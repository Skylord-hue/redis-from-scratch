#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

using namespace std;

// ============================================================================
// CHAPTER 2: THE SOCKET API (The Telephone System)
// ============================================================================
// 1. socket() -> Buying a blank phone (creates the communication endpoint)
// 2. bind()   -> Attaching a phone number (assigns IP + Port 1234)
// 3. listen() -> Turning the ringer on and waiting for incoming callers
// 4. accept() -> Picking up the phone and getting a temporary line for the caller
// ============================================================================

int main() {
    // 1. Create a blank IPv4 TCP socket (The blank phone)
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        cerr << "ERROR: Failed to create socket!" << endl;
        return 1;
    }

    // Set SO_REUSEADDR so port 1234 can be immediately reused after restarts
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // 2. Prepare the address (SIM card / Phone Number: Port 1234)
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);        // Port 1234 in network byte order (Big Endian)
    addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all network interfaces (0.0.0.0)

    // Bind socket to Port 1234
    int bind_res = ::bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (bind_res < 0) {
        cerr << "ERROR: Port 1234 is already taken!" << endl;
        close(fd);
        return 1;
    }

    // 3. Turn on the ringer (SOMAXCONN queue for pending callers)
    if (listen(fd, SOMAXCONN) < 0) {
        cerr << "ERROR: Failed to listen on socket!" << endl;
        close(fd);
        return 1;
    }
    cout << "Chapter 2 Server: Listening on Port 1234... Waiting for a call!" << endl;

    // 4. Accept the call (Blocking: freezes execution until a client dials in)
    struct sockaddr_in client_addr = {};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        cerr << "ERROR: Failed to accept incoming connection!" << endl;
        close(fd);
        return 1;
    }

    cout << "RING RING! A client connected! Assigned Client FD: #" << client_fd << endl;

    // In Chapter 2, we haven't learned read/write yet, so we gracefully hang up
    close(client_fd);
    close(fd);
    cout << "Chapter 2 complete: Handled 1 connection and exited cleanly." << endl;
    return 0;
}
