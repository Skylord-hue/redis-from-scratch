#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>      
#include <vector>
#include <cassert>
#include <cstring>
#include <fcntl.h>

using namespace std;

// ============================================================================
// CHAPTER 5: THE PROTOCOL (Framing & Length-Prefixed Buffers)
// ============================================================================
// TCP is a continuous byte stream (water hose).
// To prevent fragmentation / message gluing, we prepend a 4-byte header:
// [4-byte uint32_t length][payload]
// We use read_full() and write_all() to guarantee complete messages.
// ============================================================================

// read_full: Scoops bytes in a loop until exactly n bytes are collected
int read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t bytes_scooped = read(fd, buf, n);
        if (bytes_scooped <= 0) {
            return -1; // Disconnected or error
        }
        buf += bytes_scooped;
        n -= (size_t)bytes_scooped;
    }
    return 0; // Success: exactly n bytes read
}

// write_all: Pushes bytes in a loop until all n bytes are sent
int write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t bytes_sent = write(fd, buf, n);
        if (bytes_sent <= 0) {
            return -1;
        }
        buf += bytes_sent;
        n -= (size_t)bytes_sent;
    }
    return 0;
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

    int bind_result = ::bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (bind_result < 0) {
        cout << "ERROR: Port 1234 is already taken!" << endl;
        close(fd);
        return 1; 
    }

    listen(fd, 5000); 
    cout << "Chapter 5 Server: Listening on Port 1234 (Length-Prefixed Protocol)!" << endl;
  
    vector<struct pollfd> switchboard;

    struct pollfd server_phone = {};
    server_phone.fd = fd;
    server_phone.events = POLLIN; 
    switchboard.push_back(server_phone);

    while (true) {
        poll(switchboard.data(), switchboard.size(), -1);

        // CASE A: Main Server Phone (New Client)
        if (switchboard[0].revents & POLLIN) {
            int fd_new_client = accept(fd, nullptr, nullptr);
            if (fd_new_client >= 0) {
                cout << "RING RING! Client connected on Phone #" << fd_new_client << endl;
                struct pollfd new_client_phone = {};
                new_client_phone.fd = fd_new_client;
                new_client_phone.events = POLLIN; 
                switchboard.push_back(new_client_phone);
            }
        }

        // CASE B: Client Phone (Client is speaking)
        for (size_t i = 1; i < switchboard.size(); i++) {
            if (switchboard[i].revents & POLLIN) {
                int client_fd = switchboard[i].fd;

                // Step 1: Read the 4-byte header
                uint32_t message_length = 0;
                int err = read_full(client_fd, (char*)&message_length, 4);
                if (err < 0) {
                    cout << "Client on Phone #" << client_fd << " hung up." << endl;
                    close(client_fd);
                    switchboard.erase(switchboard.begin() + i);
                    i--; 
                    continue;
                }

                // Step 2: Validate message length
                if (message_length > 4096) {
                    cout << "Message too large! Kicking client." << endl;
                    close(client_fd);
                    switchboard.erase(switchboard.begin() + i);
                    i--; 
                    continue;
                }

                // Step 3: Read exact message payload
                char message_buffer[4096 + 1] = {}; 
                err = read_full(client_fd, message_buffer, message_length);
                if (err < 0) {
                    cout << "Client disconnected mid-message!" << endl;
                    close(client_fd);
                    switchboard.erase(switchboard.begin() + i);
                    i--; 
                    continue;
                }

                cout << "Client on Phone #" << client_fd << " said exactly: " << message_buffer << endl;

                // Step 4: Reply with length-prefixed frame
                char response[] = "Hello from Redis Chapter 5 Protocol!\n";
                uint32_t response_len = strlen(response);
                write_all(client_fd, (char*)&response_len, 4);
                write_all(client_fd, response, response_len);
            }
        }
    }

    close(fd);   
    return 0;
}
