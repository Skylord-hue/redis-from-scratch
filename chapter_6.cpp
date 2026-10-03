#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <vector>
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <cerrno>

using namespace std;

// --- THE STATE MACHINE ---
// The 3 possible states for our employee (for any given customer)
enum {
    STATE_REQ = 0, // We are trying to read their request
    STATE_RES = 1, // We are trying to send them our response
    STATE_END = 2  // The conversation is over (hang up)
};

// The Sticky Note (Every customer gets one of these)
struct Conn {
    int fd = -1;
    uint32_t state = 0; // Starts in STATE_REQ
    
    // The Reading Bucket
    size_t read_bytes = 0;
    char read_buf[4 + 4096] = {}; // 4 bytes for header + 4096 for message
    
    // The Writing Bucket
    size_t write_bytes = 0;
    size_t write_sent = 0;
    char write_buf[4 + 4096] = {};
};

// Helper: Upgrades a phone to Non-Blocking
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// --- STATE FUNCTIONS ---

// Tries to read water. If empty, puts the bucket down.
void try_read_request(Conn* conn) {
    // Try to fill the remaining empty space in the bucket
    ssize_t bytes_scooped = read(conn->fd, 
                                 &conn->read_buf[conn->read_bytes], 
                                 sizeof(conn->read_buf) - conn->read_bytes);
    
    if (bytes_scooped < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        // Pipe is empty! Do nothing. We will come back later.
        return;
    }
    
    if (bytes_scooped <= 0) {
        // Real error or client disconnected
        conn->state = STATE_END;
        return;
    }
    
    // Update the sticky note with how much we scooped
    conn->read_bytes += (size_t)bytes_scooped;
    
    // Do we have enough for the 4-byte header?
    if (conn->read_bytes < 4) {
        return; // Not enough yet. Wait for more water.
    }
    
    // We have the header! Read the promised length.
    uint32_t promised_len = 0;
    memcpy(&promised_len, conn->read_buf, 4);
    
    if (promised_len > 4096) {
        cout << "Message too large! Kicking client." << endl;
        conn->state = STATE_END;
        return;
    }
    
    // Do we have the FULL message yet?
    if (conn->read_bytes < 4 + promised_len) {
        return; // We have the header, but not the full message. Wait for more water.
    }
    
    // WE HAVE THE FULL MESSAGE!
    cout << "Successfully read full message of length: " << promised_len << endl;
    
    // Process the request and prepare the response
    char response[] = "Hello from the State Machine!";
    uint32_t res_len = strlen(response);
    
    // Fill the Writing Bucket (Header + Data)
    memcpy(conn->write_buf, &res_len, 4);
    memcpy(&conn->write_buf[4], response, res_len);
    conn->write_bytes = 4 + res_len;
    conn->write_sent = 0; // We haven't sent anything yet
    
    // Change state! We are done reading, now we need to write.
    conn->state = STATE_RES;
}

// Tries to push water back to the client
void try_write_response(Conn* conn) {
    // Try to push whatever is left in the writing bucket
    ssize_t bytes_sent = write(conn->fd, 
                               &conn->write_buf[conn->write_sent], 
                               conn->write_bytes - conn->write_sent);
                               
    if (bytes_sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        // Client's pipe is full. Try again later.
        return;
    }
    
    if (bytes_sent <= 0) {
        conn->state = STATE_END;
        return;
    }
    
    conn->write_sent += (size_t)bytes_sent;
    
    if (conn->write_sent == conn->write_bytes) {
        // We successfully sent the entire response!
        // Reset the sticky note to wait for the next request.
        conn->state = STATE_REQ;
        conn->read_bytes = 0;
        conn->write_bytes = 0;
        conn->write_sent = 0;
    }
}


int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    int val = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        cout << "ERROR: Port 1234 taken!" << endl;
        return 1;
    }

    listen(server_fd, 5000);
    set_nonblocking(server_fd);
    
    cout << "State Machine Server listening on Port 1234!" << endl;

    // We keep track of ALL sticky notes here.
    // If a slot is nullptr, it means that slot is empty.
    vector<Conn*> all_connections;

    while (true) {
        // 1. Build the clean switchboard for poll()
        vector<struct pollfd> switchboard;
        
        // Add the main server phone
        switchboard.push_back({server_fd, POLLIN, 0});
        
        // Add all active customer phones
        for (Conn* conn : all_connections) {
            if (conn == nullptr) continue;
            
            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            // If we are reading, wait for POLLIN. If writing, wait for POLLOUT.
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            switchboard.push_back(pfd);
        }

        // 2. Wait for a light to flash!
        poll(switchboard.data(), switchboard.size(), -1);

        // 3. Did the main server phone flash? (New Customer)
        if (switchboard[0].revents & POLLIN) {
            int new_client_fd = accept(server_fd, nullptr, nullptr);
            if (new_client_fd >= 0) {
                set_nonblocking(new_client_fd);
                
                // Create a brand new Sticky Note for this customer
                Conn* new_conn = new Conn();
                new_conn->fd = new_client_fd;
                new_conn->state = STATE_REQ;
                all_connections.push_back(new_conn);
            }
        }

        // 4. Did any customer phones flash?
        for (size_t i = 1; i < switchboard.size(); i++) {
            if (switchboard[i].revents) {
                int client_fd = switchboard[i].fd;
                
                // Find their sticky note
                Conn* current_conn = nullptr;
                for (Conn* c : all_connections) {
                    if (c && c->fd == client_fd) {
                        current_conn = c;
                        break;
                    }
                }
                
                if (!current_conn) continue;

                // Process them based on their current state!
                if (current_conn->state == STATE_REQ) {
                    try_read_request(current_conn);
                } else if (current_conn->state == STATE_RES) {
                    try_write_response(current_conn);
                }
                
                // If the state is END, hang up and burn the sticky note.
                if (current_conn->state == STATE_END) {
                    close(current_conn->fd);
                    
                    // Find and delete from vector
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
    return 0;
}
