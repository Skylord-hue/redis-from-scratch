#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <cstring>
#include <sstream>

using namespace std;

// ============================================================================
// CHAPTER 7 CLI CLIENT: An Interactive Redis Terminal!
// ============================================================================
// Serializes commands into:
// [4 bytes total_len][4 bytes n_args] + for each arg: [4 bytes len][data]
// Reads response:
// [4 bytes total_len][4 bytes rescode][response_string]
// ============================================================================

int read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t s = read(fd, buf, n);
        if (s <= 0) return -1;
        buf += s;
        n -= (size_t)s;
    }
    return 0;
}

int write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t s = write(fd, buf, n);
        if (s <= 0) return -1;
        buf += s;
        n -= (size_t)s;
    }
    return 0;
}

int send_command(int fd, const vector<string>& cmd) {
    // 1. Calculate total length of payload
    // 4 bytes for n_args + for each arg: 4 bytes length + arg string size
    uint32_t payload_len = 4;
    for (const auto& s : cmd) {
        payload_len += 4 + (uint32_t)s.size();
    }

    // 2. Build the buffer
    vector<char> buf(4 + payload_len);
    memcpy(buf.data(), &payload_len, 4);

    uint32_t n_args = (uint32_t)cmd.size();
    memcpy(buf.data() + 4, &n_args, 4);

    size_t pos = 8;
    for (const auto& s : cmd) {
        uint32_t len = (uint32_t)s.size();
        memcpy(buf.data() + pos, &len, 4);
        pos += 4;
        memcpy(buf.data() + pos, s.data(), len);
        pos += len;
    }

    // 3. Send over the socket
    return write_all(fd, buf.data(), buf.size());
}

int read_response(int fd) {
    uint32_t total_len = 0;
    if (read_full(fd, (char*)&total_len, 4) < 0) {
        cerr << "Server disconnected or error reading header\n";
        return -1;
    }

    if (total_len < 4) {
        cerr << "Bad response length\n";
        return -1;
    }

    uint32_t rescode = 0;
    if (read_full(fd, (char*)&rescode, 4) < 0) {
        return -1;
    }

    uint32_t data_len = total_len - 4;
    string data(data_len, '\0');
    if (data_len > 0) {
        if (read_full(fd, &data[0], data_len) < 0) {
            return -1;
        }
    }

    if (rescode == 0) {
        // RES_OK
        cout << data << endl;
    } else if (rescode == 1) {
        // RES_ERR
        cout << "(error) " << data << endl;
    } else if (rescode == 2) {
        // RES_NX
        cout << "(nil)" << endl;
    } else {
        cout << "(unknown code " << rescode << ") " << data << endl;
    }
    return 0;
}

// Tokenize user terminal input (handles quoted strings with spaces!)
vector<string> tokenize(const string& line) {
    vector<string> tokens;
    string token;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '"' || c == '\'') {
            in_quotes = !in_quotes;
        } else if (isspace(c) && !in_quotes) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token += c;
        }
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

int main(int argc, char* argv[]) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        cerr << "Failed to create socket\n";
        return 1;
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "Connection failed! Ensure Chapter 7 server is running on port 1234.\n";
        close(fd);
        return 1;
    }

    // Non-interactive mode (e.g., ./client_ch7 set foo bar)
    if (argc > 1) {
        vector<string> cmd;
        for (int i = 1; i < argc; i++) cmd.push_back(argv[i]);
        if (send_command(fd, cmd) < 0 || read_response(fd) < 0) {
            close(fd);
            return 1;
        }
        close(fd);
        return 0;
    }

    // Interactive REPL Mode (Like redis-cli!)
    cout << "Connected to Redis Chapter 7 Server (127.0.0.1:1234)\n";
    cout << "Type commands like: SET user shadow, GET user, DEL user, KEYS, or exit\n\n";

    string line;
    while (true) {
        cout << "redis 127.0.0.1:1234> ";
        if (!getline(cin, line)) break;
        if (line.empty()) continue;

        vector<string> cmd = tokenize(line);
        if (cmd.empty()) continue;

        if (cmd[0] == "exit" || cmd[0] == "quit") break;

        if (send_command(fd, cmd) < 0) {
            cerr << "Failed to send command\n";
            break;
        }
        if (read_response(fd) < 0) {
            break;
        }
    }

    close(fd);
    return 0;
}
