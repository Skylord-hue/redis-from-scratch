#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>      
#include <vector>
#include <cassert>
#include <cstring> // For memcpy
#include <fcntl.h> // fro non - blocker phones 
using namespace std;

// --- CHAPTER 5: THE PROTOCOL HELPER FUNCTIONS ---

// read_full: Wraps the normal read() in a while loop so it keeps scooping 
// water until it collects the EXACT number of bytes promised (n).
int read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t bytes_scooped = read(fd, buf, n);
        if (bytes_scooped <= 0) {
            return -1; // Client disconnected or error
        }
        // Move the buffer pointer forward so we don't overwrite what we just read
        buf += bytes_scooped;
        // Subtract what we scooped from the total amount we still need
        n -= (size_t)bytes_scooped;
    }
    return 0; // Success! We read exactly all n bytes.
}

// write_all: Just like read_full, but for speaking. If the internet is slow, 
// write() might only send half the word. This loop ensures the whole word is sent.
int write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t bytes_sent = write(fd, buf, n);
        if (bytes_sent <= 0) {
            return -1; // Error
        }
        buf += bytes_sent;
        n -= (size_t)bytes_sent;
    }
    return 0;
}


int main()
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;         
  addr.sin_port = htons(1234);       
  addr.sin_addr.s_addr = INADDR_ANY; 

  int bind_result = ::bind(fd, (struct sockaddr *)&addr, sizeof(addr));
  if (bind_result < 0) {
    cout << "ERROR: The phone number (Port 1234) is already taken!" << endl;
    return 1; 
  }

  listen(fd, 5000); 
  cout << "Server is listening on Port 1234! Waiting for a call..." << endl;
  
  vector<struct pollfd> switchboard;

  struct pollfd server_phone = {};
  server_phone.fd = fd;
  server_phone.events = POLLIN; 
  switchboard.push_back(server_phone);

  while (true)
  {
      poll(switchboard.data(), switchboard.size(), -1);

      // CASE A: Main Server Phone (New Client)
      if (switchboard[0].revents & POLLIN) 
      {
          int fd_new_client = accept(fd, nullptr, nullptr);
          cout << "RING RING! A new client connected! Handing them Phone #" << fd_new_client << endl;
          
          struct pollfd new_client_phone = {};
          new_client_phone.fd = fd_new_client;
          new_client_phone.events = POLLIN; 
          switchboard.push_back(new_client_phone);
      }

      // CASE B: Client Phone (Client is speaking)
      for (size_t i = 1; i < switchboard.size(); i++)
      {
          if (switchboard[i].revents & POLLIN)
          {
              int client_fd = switchboard[i].fd;
              
              // --- CHAPTER 5 PROTOCOL IN ACTION ---

              // Step 1: Read the Header (exactly 4 bytes)
              uint32_t message_length = 0;
              int err = read_full(client_fd, (char*)&message_length, 4);
              
              if (err < 0) {
                  cout << "Client on Phone #" << client_fd << " hung up." << endl;
                  close(client_fd);
                  switchboard.erase(switchboard.begin() + i);
                  i--; 
                  continue; // Skip the rest of this loop
              }

              // Step 2: Read the actual message based on the length promised!
              if (message_length > 4096) {
                 cout << "Message too large! Kicking client." << endl;
                 close(client_fd);
                 switchboard.erase(switchboard.begin() + i);
                 i--; 
                 continue;
              }

              // Create a temporary buffer that is exactly the size of the message
              char message_buffer[4096] = {}; 
              err = read_full(client_fd, message_buffer, message_length);
              
              if (err < 0) {
                  cout << "Client lied about message length or disconnected mid-sentence!" << endl;
                  close(client_fd);
                  switchboard.erase(switchboard.begin() + i);
                  i--; 
                  continue;
              }

              // Step 3: We successfully grabbed the exact message! Process it.
              cout << "Client on Phone #" << client_fd << " said exactly: " << message_buffer << endl;
              
              // Step 4: Reply using the Protocol (Header + Data)
              char response[] = "Hello from Redis Protocol!\n";
              uint32_t response_len = strlen(response);
              
              // Send 4-byte header first, then send the response string
              write_all(client_fd, (char*)&response_len, 4);
              write_all(client_fd, response, response_len);
          }
      }
  }

  close(fd);   
  return 0;
}
