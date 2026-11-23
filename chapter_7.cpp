#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <vector>
#include <string>
#include <map>
#include <cstring>
#include <fcntl.h>
#include <cerrno>

using namespace std;

// ============================================================================
// CHAPTER 7: THE IN-MEMORY KEY-VALUE DATABASE (GET, SET, DEL)
// ============================================================================
// 1. Unpacks incoming byte stream into command tokens: vector<string> cmd
// 2. Dispatches cmd[0] to do_get(), do_set(), do_del()
// 3. Modifies our in-memory dictionary: std::map<string, string> g_map
// 4. Encodes response with a status code (RES_OK, RES_ERR, RES_NX) + 4-byte header
// ============================================================================

// --- STATUS CODES ---
enum {
    RES_OK  = 0, // Success
    RES_ERR = 1, // Error / Invalid syntax / Unknown command
    RES_NX  = 2  // Key Not Found (nil)
};

// --- STATE MACHINE STATES ---
enum {
    STATE_REQ = 0, // Reading request from client
    STATE_RES = 1, // Sending response back to client
    STATE_END = 2  // Connection marked for termination
};

// --- THE STICKY NOTE (Per-Connection State & Buffers) ---
struct Conn {
    int fd = -1;
    uint32_t state = STATE_REQ;

    // Incoming Request Bucket
    size_t read_bytes = 0;
    char read_buf[4 + 4096] = {};

    // Outgoing Response Bucket
    size_t write_bytes = 0;
    size_t write_sent = 0;
    char write_buf[4 + 4096] = {};
};

// --- THE DATABASE (In-Memory Key-Value Store) ---
// (In Chapter 8, we replace this std::map with our own raw-pointer Hash Table!)
static map<string, string> g_map;

// Helper: Sets socket to non-blocking
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// ============================================================================
// PROTOCOL PARSER: Unpacks raw binary payload into vector<string> cmd
// ============================================================================
// Payload layout:
// [4 bytes n_args] + for each arg: [4 bytes len] + [len bytes string data]
int parse_req(const char* data, size_t total_len, vector<string>& cmd) {
    if (total_len < 4) return -1;

    uint32_t n_args = 0;
    memcpy(&n_args, data, 4);

    size_t pos = 4;
    for (uint32_t i = 0; i < n_args; i++) {
        if (pos + 4 > total_len) return -1; // Incomplete argument header

        uint32_t arg_len = 0;
        memcpy(&arg_len, data + pos, 4);
        pos += 4;

        if (pos + arg_len > total_len) return -1; // Incomplete argument body

        cmd.emplace_back(data + pos, arg_len);
        pos += arg_len;
    }

    if (pos != total_len) return -1; // Trailing garbage detected!
    return 0;
}

// ============================================================================
// COMMAND HANDLERS
// ============================================================================
void do_get(const vector<string>& cmd, uint32_t& rescode, string& out) {
    if (cmd.size() != 2) {
        rescode = RES_ERR;
        out = "ERR syntax error: GET requires exactly 1 key";
        return;
    }
    auto it = g_map.find(cmd[1]);
    if (it == g_map.end()) {
        rescode = RES_NX;
        out = "(nil)";
        return;
    }
    rescode = RES_OK;
    out = it->second;
}

void do_set(const vector<string>& cmd, uint32_t& rescode, string& out) {
    if (cmd.size() != 3) {
        rescode = RES_ERR;
        out = "ERR syntax error: SET requires key and value";
        return;
    }
    g_map[cmd[1]] = cmd[2];
    rescode = RES_OK;
    out = "OK";
}

void do_del(const vector<string>& cmd, uint32_t& rescode, string& out) {
    if (cmd.size() != 2) {
        rescode = RES_ERR;
        out = "ERR syntax error: DEL requires exactly 1 key";
        return;
    }
    auto it = g_map.find(cmd[1]);
    if (it != g_map.end()) {
        g_map.erase(it);
        rescode = RES_OK;
        out = "(integer) 1";
    } else {
        rescode = RES_OK;
        out = "(integer) 0";
    }
}

void do_keys(uint32_t& rescode, string& out) {
    rescode = RES_OK;
    if (g_map.empty()) {
        out = "(empty list or set)";
        return;
    }
    string result = "";
    int idx = 1;
    for (const auto& pair : g_map) {
        result += to_string(idx++) + ") \"" + pair.first + "\"\n";
    }
    if (!result.empty() && result.back() == '\n') result.pop_back();
    out = result;
}

// Command Router (Checks cmd[0] case-insensitively)
void do_request(const vector<string>& cmd, uint32_t& rescode, string& out) {
    if (cmd.empty()) {
        rescode = RES_ERR;
        out = "ERR empty command";
        return;
    }

    string op = cmd[0];
    for (char &c : op) c = tolower(c);

    if (op == "get") {
        do_get(cmd, rescode, out);
    } else if (op == "set") {
        do_set(cmd, rescode, out);
    } else if (op == "del") {
        do_del(cmd, rescode, out);
    } else if (op == "keys") {
        do_keys(rescode, out);
    } else {
        rescode = RES_ERR;
        out = "ERR unknown command '" + cmd[0] + "'";
    }
}

// ============================================================================
// STATE MACHINE HANDLERS
// ============================================================================
void try_read_request(Conn* conn) {
    ssize_t bytes_scooped = read(conn->fd,
                                 &conn->read_buf[conn->read_bytes],
                                 sizeof(conn->read_buf) - conn->read_bytes);

    if (bytes_scooped < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return; // OS pipe empty right now
    }

    if (bytes_scooped <= 0) {
        conn->state = STATE_END; // Client hung up or network error
        return;
    }

    conn->read_bytes += (size_t)bytes_scooped;

    // Check if we have at least 4 bytes for the outer header
    if (conn->read_bytes < 4) return;

    uint32_t total_len = 0;
    memcpy(&total_len, conn->read_buf, 4);

    if (total_len > 4096) {
        cout << "[!] Message too large (" << total_len << " bytes). Disconnecting.\n";
        conn->state = STATE_END;
        return;
    }

    // Check if full payload has arrived
    if (conn->read_bytes < 4 + total_len) return;

    // --- FULL MESSAGE RECEIVED: UNPACK & EXECUTE ---
    vector<string> cmd;
    if (parse_req(&conn->read_buf[4], total_len, cmd) < 0) {
        cout << "[!] Bad request format from client #" << conn->fd << endl;
        conn->state = STATE_END;
        return;
    }

    // Print command execution log
    cout << "[Client " << conn->fd << "] Executing: ";
    for (const auto& token : cmd) cout << "\"" << token << "\" ";
    cout << endl;

    // Execute against database
    uint32_t rescode = RES_OK;
    string res_data;
    do_request(cmd, rescode, res_data);

    // --- PACKAGE THE RESPONSE INTO WRITE_BUF ---
    // Response layout:
    // [4 bytes total response length] + [4 bytes rescode] + [response string]
    uint32_t res_payload_len = 4 + (uint32_t)res_data.size();
    memcpy(conn->write_buf, &res_payload_len, 4);
    memcpy(&conn->write_buf[4], &rescode, 4);
    memcpy(&conn->write_buf[8], res_data.data(), res_data.size());

    conn->write_bytes = 4 + res_payload_len;
    conn->write_sent = 0;
    conn->state = STATE_RES; // Switch to writing!
}

void try_write_response(Conn* conn) {
    ssize_t bytes_sent = write(conn->fd,
                               &conn->write_buf[conn->write_sent],
                               conn->write_bytes - conn->write_sent);

    if (bytes_sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return; // Client receive buffer full, wait for next tick
    }

    if (bytes_sent <= 0) {
        conn->state = STATE_END;
        return;
    }

    conn->write_sent += (size_t)bytes_sent;

    // Check if entire response has been sent
    if (conn->write_sent == conn->write_bytes) {
        conn->state = STATE_REQ; // Reset state machine to wait for next command
        conn->read_bytes = 0;
        conn->write_bytes = 0;
        conn->write_sent = 0;
    }
}

// ============================================================================
// MAIN EVENT LOOP
// ============================================================================
int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        cerr << "socket() failed\n";
        return 1;
    }

    int val = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        cerr << "bind() failed: Port 1234 taken!\n";
        close(server_fd);
        return 1;
    }

    listen(server_fd, 5000);
    set_nonblocking(server_fd);

    cout << "========================================================\n";
    cout << " Chapter 7 Redis Server (Key-Value Engine) on Port 1234\n";
    cout << " Commands supported: GET <key>, SET <key> <val>, DEL <key>, KEYS\n";
    cout << "========================================================\n";

    vector<Conn*> all_connections;

    while (true) {
        vector<struct pollfd> switchboard;
        switchboard.push_back({server_fd, POLLIN, 0});

        for (Conn* conn : all_connections) {
            if (!conn) continue;
            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            switchboard.push_back(pfd);
        }

        int activity = poll(switchboard.data(), switchboard.size(), -1);
        if (activity < 0) {
            if (errno == EINTR) continue;
            cerr << "poll() error\n";
            break;
        }

        // Handle new client connection
        if (switchboard[0].revents & POLLIN) {
            int new_fd = accept(server_fd, nullptr, nullptr);
            if (new_fd >= 0) {
                set_nonblocking(new_fd);
                Conn* new_conn = new Conn();
                new_conn->fd = new_fd;
                new_conn->state = STATE_REQ;
                all_connections.push_back(new_conn);
                cout << "[+] Client connected: FD #" << new_fd << endl;
            }
        }

        // Handle client I/O
        for (size_t i = 1; i < switchboard.size(); i++) {
            if (switchboard[i].revents) {
                int client_fd = switchboard[i].fd;

                Conn* current_conn = nullptr;
                for (Conn* c : all_connections) {
                    if (c && c->fd == client_fd) {
                        current_conn = c;
                        break;
                    }
                }
                if (!current_conn) continue;

                if (current_conn->state == STATE_REQ) {
                    try_read_request(current_conn);
                } else if (current_conn->state == STATE_RES) {
                    try_write_response(current_conn);
                }

                // If marked for closure
                if (current_conn->state == STATE_END) {
                    cout << "[-] Client disconnected: FD #" << current_conn->fd << endl;
                    close(current_conn->fd);
                    for (size_t j = 0; j < all_connections.size(); j++) {
                        if (all_connections[j] == current_conn) {
                            delete current_conn;
                            all_connections[j] = nullptr;
                            break;
                        }
                    }
                }
            }
        }
    }

    close(server_fd);
    return 0;
}
