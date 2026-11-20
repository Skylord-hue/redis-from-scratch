#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <vector>
#include <cstring>

using namespace std;

// ============================================================================
// CHAPTER 4: THE EVENT LOOP (Single-Threaded I/O Multiplexing)
// ============================================================================
// Why multithreading is a trap: 10,000 threads = massive RAM & CPU context switching.
// Solution:
// - poll(): A giant switchboard that monitors all client file descriptors.
// - The single employee sits in a spinny chair (while(true)) and only attends
//   to phones whose red light flashes (POLLIN).
// ============================================================================

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        cerr << "ERROR: Failed to create server socket!" << endl;
        return 1;
    }

    int val = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "ERROR: Port 1234 taken!" << endl;
        close(server_fd);
        return 1;
    }

    listen(server_fd, 5000);
    cout << "Chapter 4 Server: Event Loop listening on Port 1234 (Single-Threaded poll)!" << endl;

    // The Switchboard: array of phones we monitor
    vector<struct pollfd> switchboard;

    // Slot 0 is always the main server phone
    struct pollfd server_phone = {};
    server_phone.fd = server_fd;
    server_phone.events = POLLIN; // Wait for incoming calls
    switchboard.push_back(server_phone);

    char buffer[1024] = {};

    while (true) {
        // Wait until at least one phone has activity (-1 means wait indefinitely)
        int activity = poll(switchboard.data(), switchboard.size(), -1);
        if (activity < 0) {
            cerr << "ERROR: poll() failed!" << endl;
            break;
        }

        // CASE A: Main listening phone rang -> New caller arrived!
        if (switchboard[0].revents & POLLIN) {
            int client_fd = accept(server_fd, nullptr, nullptr);
            if (client_fd >= 0) {
                cout << "RING RING! New client connected! Handing them Phone #" << client_fd << endl;
                struct pollfd client_phone = {};
                client_phone.fd = client_fd;
                client_phone.events = POLLIN; // Wait for client to speak
                switchboard.push_back(client_phone);
            }
        }

        // CASE B: An existing client phone has data ready to read
        for (size_t i = 1; i < switchboard.size(); i++) {
            if (switchboard[i].revents & POLLIN) {
                int client_fd = switchboard[i].fd;
                memset(buffer, 0, sizeof(buffer));

                ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read <= 0) {
                    cout << "Client on Phone #" << client_fd << " disconnected or error." << endl;
                    close(client_fd);
                    switchboard.erase(switchboard.begin() + i);
                    i--; // Adjust index after erase
                    continue;
                }

                cout << "Client #" << client_fd << " says: " << buffer;

                // Echo acknowledgment back
                const char response[] = "Echo from Chapter 4 Event Loop\n";
                write(client_fd, response, strlen(response));
            }
        }
    }

    close(server_fd);
    return 0;
}
