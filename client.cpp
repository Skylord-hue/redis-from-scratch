#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>

using namespace std;

// ============================================================================
// TEST CLIENT FOR CHAPTERS 5 & 6 (Length-Prefixed Framing)
// ============================================================================
// Sends framed messages: [uint32_t length][payload]
// Reads framed replies:  [uint32_t length][payload]
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

int send_req(int fd, const string &text) {
    uint32_t len = (uint32_t)text.size();
    if (len > 4096) return -1;

    // Send 4-byte header then body
    if (write_all(fd, (char*)&len, 4) < 0) return -1;
    if (write_all(fd, text.data(), len) < 0) return -1;
    return 0;
}

int read_res(int fd) {
    // Read 4-byte header
    uint32_t len = 0;
    if (read_full(fd, (char*)&len, 4) < 0) {
        cerr << "EOF / read error on response header" << endl;
        return -1;
    }
    if (len > 4096) {
        cerr << "Response too large: " << len << endl;
        return -1;
    }

    char buf[4096 + 1] = {};
    if (read_full(fd, buf, len) < 0) {
        cerr << "Failed reading response body" << endl;
        return -1;
    }

    cout << "Server response (" << len << " bytes): " << buf << endl;
    return 0;
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        cerr << "Failed to create socket" << endl;
        return 1;
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "Connection failed! Make sure the server is running on port 1234." << endl;
        close(fd);
        return 1;
    }
    cout << "Connected to Redis server on 127.0.0.1:1234!" << endl;

    // Send test requests
    send_req(fd, "ping");
    read_res(fd);

    send_req(fd, "hello from client");
    read_res(fd);

    close(fd);
    return 0;
}
