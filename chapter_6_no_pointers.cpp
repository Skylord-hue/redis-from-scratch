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

// The 3 possible states for our employee
enum {
    STATE_REQ = 0, 
    STATE_RES = 1, 
    STATE_END = 2  
};

// The Sticky Note
struct Conn {
    int fd = -1;
    uint32_t state = 0; 
    
    size_t read_bytes = 0;
    char read_buf[4 + 4096] = {}; 
    
    size_t write_bytes = 0;
    size_t write_sent = 0;
    char write_buf[4 + 4096] = {};
};

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Notice the '&' - we are passing by REFERENCE instead of using pointers!
void try_read_request(Conn& conn) {
    ssize_t bytes_scooped = read(conn.fd, 
                                 &conn.read_buf[conn.read_bytes], 
                                 sizeof(conn.read_buf) - conn.read_bytes);
    
    if (bytes_scooped < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return;
    }
    
    if (bytes_scooped <= 0) {
        conn.state = STATE_END;
        return;
    }
    
    conn.read_bytes += (size_t)bytes_scooped;
    
    if (conn.read_bytes < 4) {
        return; 
    }
    
    uint32_t promised_len = 0;
    memcpy(&promised_len, conn.read_buf, 4);
    
    if (promised_len > 4096) {
        conn.state = STATE_END;
        return;
    }
    
    if (conn.read_bytes < 4 + promised_len) {
        return; 
    }
    
    cout << "Successfully read full message of length: " << promised_len << endl;
    
    char response[] = "Hello from the No-Pointer State Machine!";
    uint32_t res_len = strlen(response);
    
    memcpy(conn.write_buf, &res_len, 4);
    memcpy(&conn.write_buf[4], response, res_len);
    conn.write_bytes = 4 + res_len;
    conn.write_sent = 0; 
    
    conn.state = STATE_RES;
}

// Passed by REFERENCE
void try_write_response(Conn& conn) {
    ssize_t bytes_sent = write(conn.fd, 
                               &conn.write_buf[conn.write_sent], 
                               conn.write_bytes - conn.write_sent);
                               
    if (bytes_sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return;
    }
    
    if (bytes_sent <= 0) {
        conn.state = STATE_END;
        return;
    }
    
    conn.write_sent += (size_t)bytes_sent;
    
    if (conn.write_sent == conn.write_bytes) {
        conn.state = STATE_REQ;
        conn.read_bytes = 0;
        conn.write_bytes = 0;
        conn.write_sent = 0;
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

    ::bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 5000);
    set_nonblocking(server_fd);
    
    cout << "Server listening on Port 1234!" << endl;

    // NO POINTERS! Just a normal vector of regular Conn objects.
    vector<Conn> all_connections;

    while (true) {
        vector<struct pollfd> switchboard;
        switchboard.push_back({server_fd, POLLIN, 0});
        
        // No pointers!
        for (Conn& conn : all_connections) {
            if (conn.fd == -1) continue; // Skip empty slots
            
            struct pollfd pfd = {};
            pfd.fd = conn.fd;
            pfd.events = (conn.state == STATE_REQ) ? POLLIN : POLLOUT;
            switchboard.push_back(pfd);
        }

        poll(switchboard.data(), switchboard.size(), -1);

        if (switchboard[0].revents & POLLIN) {
            int new_client_fd = accept(server_fd, nullptr, nullptr);
            if (new_client_fd >= 0) {
                set_nonblocking(new_client_fd);
                
                // NO 'new' KEYWORD! Just a normal object.
                Conn new_conn = {};
                new_conn.fd = new_client_fd;
                new_conn.state = STATE_REQ;
                all_connections.push_back(new_conn);
            }
        }

        for (size_t i = 1; i < switchboard.size(); i++) {
            if (switchboard[i].revents) {
                int client_fd = switchboard[i].fd;
                
                // Find their sticky note by reference
                Conn* current_conn = nullptr;
                for (Conn& c : all_connections) {
                    if (c.fd == client_fd) {
                        current_conn = &c;
                        break;
                    }
                }
                
                if (!current_conn) continue;

                if (current_conn->state == STATE_REQ) {
                    // Pass the object directly (dereferencing the finder pointer)
                    try_read_request(*current_conn);
                } else if (current_conn->state == STATE_RES) {
                    try_write_response(*current_conn);
                }
                
                if (current_conn->state == STATE_END) {
                    close(current_conn->fd);
                    
                    // Mark as empty instead of deleting memory
                    current_conn->fd = -1;
                }
            }
        }
    }
    return 0;
}
