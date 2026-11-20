#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

using namespace std;

// ============================================================================
// CHAPTER 3: THE TCP SERVER & CLIENT (Reading & Writing)
// ============================================================================
// Characters introduced:
// - read():  Blind worker scooping raw bytes from the network pipe into RAM buffer
// - write(): Speaks raw bytes back into the client's network pipe
// - close(): Hangs up the line so file descriptors don't leak
// ============================================================================

void handle_client(int client_fd) {
    char buffer[1024] = {};

    while (true) {
        // Clear buffer before each read
        memset(buffer, 0, sizeof(buffer));

        // 1. Scoop raw bytes from the client line into our buffer
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);

        if (bytes_read < 0) {
            cerr << "ERROR: read() failed on client #" << client_fd << endl;
            break;
        }
        if (bytes_read == 0) {
            cout << "Client #" << client_fd << " hung up (EOF)." << endl;
            break;
        }

        cout << "Client #" << client_fd << " said (" << bytes_read << " bytes): " << buffer;

        // 2. Speak back to the client
        const char response[] = "ACK from Redis Chapter 3 Server\n";
        ssize_t bytes_written = write(client_fd, response, strlen(response));
        if (bytes_written < 0) {
            cerr << "ERROR: write() failed on client #" << client_fd << endl;
            break;
        }
    }

    // 3. Close the client connection
    close(client_fd);
    cout << "Closed connection for client #" << client_fd << endl;
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        cerr << "ERROR: Failed to create socket!" << endl;
        return 1;
    }

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "ERROR: Port 1234 already taken!" << endl;
        close(fd);
        return 1;
    }

    if (listen(fd, SOMAXCONN) < 0) {
        cerr << "ERROR: listen() failed!" << endl;
        close(fd);
        return 1;
    }
    cout << "Chapter 3 Server: Listening on Port 1234 (Sequential 1-client server)..." << endl;

    // Serves clients sequentially (Flaw: other clients wait while one is connected!)
    while (true) {
        int client_fd = accept(fd, nullptr, nullptr);
        if (client_fd < 0) {
            cerr << "ERROR: accept() failed!" << endl;
            continue;
        }
        cout << "New client connected on fd #" << client_fd << endl;
        handle_client(client_fd);
    }

    close(fd);
    return 0;
}
