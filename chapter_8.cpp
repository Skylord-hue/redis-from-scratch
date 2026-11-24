#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <vector>
#include <string>
#include <cstring>
#include <fcntl.h>
#include <cerrno>

using namespace std;

// ============================================================================
// CHAPTER 8: THE CUSTOM HASH TABLE (Built from Raw Memory & Pointers)
// ============================================================================
// Goodbye std::map! Today we build our own O(1) in-memory Hash Table:
// 1. Array of Pointers (The Drawers / Buckets)
// 2. The Formula (FNV-1a Hash Function) mapping any word -> bucket index
// 3. Singly-Linked List Chaining to handle collisions when keys land in the same bucket
// 4. Exact same networking layer from Chapter 7!
// ============================================================================

// --- STATUS CODES ---
enum {
    RES_OK  = 0, // Success
    RES_ERR = 1, // Error / Syntax error
    RES_NX  = 2  // Key Not Found (nil)
};

// --- STATE MACHINE STATES ---
enum {
    STATE_REQ = 0, // Reading request
    STATE_RES = 1, // Sending response
    STATE_END = 2  // Marked for disconnect
};

// --- THE STICKY NOTE (Per-Connection Buffers for the Network) ---
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

// ============================================================================
// PART 1: THE CUSTOM HASH TABLE DATA STRUCTURES
// ============================================================================

// The Linked List Node (A letter inside a drawer)
struct Entry {
    string key;
    string val;
    Entry* next = nullptr; // Pointer to the next entry if two keys collide in the same drawer
};

// The Global Warehouse (The Cabinet of Drawers)
struct CustomHashTable {
    Entry** buckets = nullptr; // Array of pointers! Each slot is a pointer to the head of a list
    size_t capacity = 16;      // Initial 16 drawers (we expand this in Chapter 9!)
    size_t size = 0;          // Total number of active keys stored

    // Constructor: Allocates the array of pointers in heap RAM
    CustomHashTable(size_t cap = 16) : capacity(cap), size(0) {
        buckets = new Entry*[capacity](); // The () initializes all pointers to nullptr!
    }

    // Destructor: Cleans up all heap nodes on shutdown
    ~CustomHashTable() {
        clear();
        delete[] buckets;
    }

    // THE FORMULA: FNV-1a Hash Algorithm
    // Takes any string and scrambles it into a 64-bit integer
    static uint64_t hash_function(const string& key) {
        uint64_t hash = 14695981039346656037ULL; // 64-bit FNV offset basis
        for (char c : key) {
            hash ^= (uint8_t)c;
            hash *= 1099511628211ULL;            // 64-bit FNV prime
        }
        return hash;
    }

    // Helper: Map the giant hash number to an exact drawer index (0 to capacity - 1)
    size_t get_bucket_index(const string& key) const {
        return hash_function(key) % capacity;
    }

    // --- OPERATION 1: LOOKUP (GET) ---
    // Instant O(1) jump to the drawer, then check the short chain!
    bool get(const string& key, string& out_val) {
        size_t idx = get_bucket_index(key);
        Entry* curr = buckets[idx];

        while (curr) {
            if (curr->key == key) {
                out_val = curr->val;
                return true; // Found!
            }
            curr = curr->next;
        }
        return false; // Not found
    }

    // --- OPERATION 2: INSERT / UPDATE (SET) ---
    // If key exists, update value in place!
    // If key is new, attach a new node to the front of this drawer's chain!
    void set(const string& key, const string& val) {
        size_t idx = get_bucket_index(key);
        Entry* curr = buckets[idx];

        // 1. Check if the key already exists in this drawer
        while (curr) {
            if (curr->key == key) {
                curr->val = val; // Overwrite in place!
                return;
            }
            curr = curr->next;
        }

        // 2. Key is new! Create a new node and insert at HEAD of the bucket
        Entry* new_node = new Entry{key, val, buckets[idx]};
        buckets[idx] = new_node;
        size++;
    }

    // --- OPERATION 3: DELETE (DEL) ---
    // Unlinks the node from the chain and frees heap memory
    bool del(const string& key) {
        size_t idx = get_bucket_index(key);
        Entry* curr = buckets[idx];
        Entry* prev = nullptr;

        while (curr) {
            if (curr->key == key) {
                if (prev) {
                    prev->next = curr->next; // Bypass curr
                } else {
                    buckets[idx] = curr->next; // Head of the bucket was deleted
                }
                delete curr;
                size--;
                return true;
            }
            prev = curr;
            curr = curr->next;
        }
        return false;
    }

    // --- OPERATION 4: LIST ALL KEYS (KEYS) ---
    vector<string> all_keys() const {
        vector<string> res;
        for (size_t i = 0; i < capacity; i++) {
            Entry* curr = buckets[i];
            while (curr) {
                res.push_back(curr->key);
                curr = curr->next;
            }
        }
        return res;
    }

    // Clean up all nodes
    void clear() {
        for (size_t i = 0; i < capacity; i++) {
            Entry* curr = buckets[i];
            while (curr) {
                Entry* next = curr->next;
                delete curr;
                curr = next;
            }
            buckets[i] = nullptr;
        }
        size = 0;
    }
};

// ============================================================================
// THE ONE GLOBAL WAREHOUSE (Shared by ALL clients on the switchboard!)
// ============================================================================
static CustomHashTable g_table(16);

// Helper: Sets socket to non-blocking
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// ============================================================================
// PROTOCOL PARSER (From Chapter 7: Unpacks payload into vector<string> cmd)
// ============================================================================
int parse_req(const char* data, size_t total_len, vector<string>& cmd) {
    if (total_len < 4) return -1;

    uint32_t n_args = 0;
    memcpy(&n_args, data, 4);

    size_t pos = 4;
    for (uint32_t i = 0; i < n_args; i++) {
        if (pos + 4 > total_len) return -1;

        uint32_t arg_len = 0;
        memcpy(&arg_len, data + pos, 4);
        pos += 4;

        if (pos + arg_len > total_len) return -1;

        cmd.emplace_back(data + pos, arg_len);
        pos += arg_len;
    }

    if (pos != total_len) return -1;
    return 0;
}

// ============================================================================
// COMMAND HANDLERS (Now operating on our CustomHashTable!)
// ============================================================================
void do_get(const vector<string>& cmd, uint32_t& rescode, string& out) {
    if (cmd.size() != 2) {
        rescode = RES_ERR;
        out = "ERR syntax error: GET requires exactly 1 key";
        return;
    }

    string val;
    if (g_table.get(cmd[1], val)) {
        rescode = RES_OK;
        out = val;
    } else {
        rescode = RES_NX;
        out = "(nil)";
    }
}

void do_set(const vector<string>& cmd, uint32_t& rescode, string& out) {
    if (cmd.size() != 3) {
        rescode = RES_ERR;
        out = "ERR syntax error: SET requires key and value";
        return;
    }

    g_table.set(cmd[1], cmd[2]);
    rescode = RES_OK;
    out = "OK";
}

void do_del(const vector<string>& cmd, uint32_t& rescode, string& out) {
    if (cmd.size() != 2) {
        rescode = RES_ERR;
        out = "ERR syntax error: DEL requires exactly 1 key";
        return;
    }

    if (g_table.del(cmd[1])) {
        rescode = RES_OK;
        out = "(integer) 1";
    } else {
        rescode = RES_OK;
        out = "(integer) 0";
    }
}

void do_keys(uint32_t& rescode, string& out) {
    rescode = RES_OK;
    vector<string> keys = g_table.all_keys();
    if (keys.empty()) {
        out = "(empty list or set)";
        return;
    }
    string result = "";
    int idx = 1;
    for (const auto& k : keys) {
        result += to_string(idx++) + ") \"" + k + "\"\n";
    }
    if (!result.empty() && result.back() == '\n') result.pop_back();
    out = result;
}

// Command Router
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
// STATE MACHINE HANDLERS (From Chapter 6 & 7)
// ============================================================================
void try_read_request(Conn* conn) {
    ssize_t bytes_scooped = read(conn->fd,
                                 &conn->read_buf[conn->read_bytes],
                                 sizeof(conn->read_buf) - conn->read_bytes);

    if (bytes_scooped < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return; // Empty pipe
    }

    if (bytes_scooped <= 0) {
        conn->state = STATE_END;
        return;
    }

    conn->read_bytes += (size_t)bytes_scooped;

    if (conn->read_bytes < 4) return;

    uint32_t total_len = 0;
    memcpy(&total_len, conn->read_buf, 4);

    if (total_len > 4096) {
        cout << "[!] Message too large (" << total_len << " bytes). Disconnecting.\n";
        conn->state = STATE_END;
        return;
    }

    if (conn->read_bytes < 4 + total_len) return;

    // Full command ready! Parse and execute
    vector<string> cmd;
    if (parse_req(&conn->read_buf[4], total_len, cmd) < 0) {
        conn->state = STATE_END;
        return;
    }

    cout << "[Client " << conn->fd << "] Executing: ";
    for (const auto& token : cmd) cout << "\"" << token << "\" ";
    cout << endl;

    uint32_t rescode = RES_OK;
    string res_data;
    do_request(cmd, rescode, res_data);

    // Package response into write_buf
    uint32_t res_payload_len = 4 + (uint32_t)res_data.size();
    memcpy(conn->write_buf, &res_payload_len, 4);
    memcpy(&conn->write_buf[4], &rescode, 4);
    memcpy(&conn->write_buf[8], res_data.data(), res_data.size());

    conn->write_bytes = 4 + res_payload_len;
    conn->write_sent = 0;
    conn->state = STATE_RES;
}

void try_write_response(Conn* conn) {
    ssize_t bytes_sent = write(conn->fd,
                               &conn->write_buf[conn->write_sent],
                               conn->write_bytes - conn->write_sent);

    if (bytes_sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return;
    }

    if (bytes_sent <= 0) {
        conn->state = STATE_END;
        return;
    }

    conn->write_sent += (size_t)bytes_sent;

    if (conn->write_sent == conn->write_bytes) {
        conn->state = STATE_REQ; // Reset for next command!
        conn->read_bytes = 0;
        conn->write_bytes = 0;
        conn->write_sent = 0;
    }
}

// ============================================================================
// MAIN EVENT LOOP (Untouched from Chapter 6 & 7)
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
    cout << " Chapter 8 Redis Server (Custom Hash Table Engine) on Port 1234\n";
    cout << " Storage: Raw Pointer Array + FNV-1a Hash + Chained Linked List\n";
    cout << " Commands: GET <key>, SET <key> <val>, DEL <key>, KEYS\n";
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

        // New client connection
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

        // Client I/O
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
