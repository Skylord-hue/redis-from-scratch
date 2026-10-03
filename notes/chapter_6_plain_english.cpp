#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <vector>
#include <cstring>
#include <fcntl.h>
#include <cerrno>

using namespace std;

// =====================================================================
// PART 1: THE BLUEPRINTS (What things look like)
// =====================================================================

// We have 3 "States" our employee can be in when talking to a customer.
enum {
    STATE_READING_REQUEST = 0, // Trying to scoop water from the pipe
    STATE_WRITING_RESPONSE = 1, // Trying to push water back into the pipe
    STATE_HANG_UP = 2          // The conversation is over
};

// The Sticky Note
// Because the phones never freeze (Non-Blocking), our employee has to put 
// the phone down if the pipe is empty. They need a sticky note to remember
// what they were doing when they come back later.
struct StickyNote {
    int client_phone_id = -1;
    uint32_t current_state = STATE_READING_REQUEST; 
    
    // The Reading Bucket (To catch incoming water)
    size_t bytes_scooped_so_far = 0;
    char read_bucket[4096 + 4] = {}; 
    
    // The Writing Bucket (To hold the water we want to send back)
    size_t total_bytes_to_send = 0;
    size_t bytes_successfully_sent = 0;
    char write_bucket[4096 + 4] = {};
};

// A magic tool to upgrade any phone to Non-Blocking so it never freezes
void upgrade_phone_to_nonblocking(int fd) {
    int current_settings = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, current_settings | O_NONBLOCK);
}

// =====================================================================
// PART 2: THE EMPLOYEE'S RULEBOOK (State Machine Functions)
// =====================================================================

// RULE 1: What to do when trying to READ water
void handle_reading_request(StickyNote& note) {
    // 1. Scoop whatever water is available in the pipe right now
    ssize_t water_scooped = read(note.client_phone_id, 
                                 &note.read_bucket[note.bytes_scooped_so_far], 
                                 sizeof(note.read_bucket) - note.bytes_scooped_so_far);
    
    // 2. Check if the pipe was just temporarily empty
    if (water_scooped < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return; // The pipe is empty! Put the phone down and go back to the switchboard.
    }
    
    // 3. Check if the client actually hung up on us
    if (water_scooped <= 0) {
        note.current_state = STATE_HANG_UP;
        return;
    }
    
    // 4. Write down how much water we successfully scooped
    note.bytes_scooped_so_far += (size_t)water_scooped;
    
    // 5. Do we have at least 4 bytes yet? (The Header)
    if (note.bytes_scooped_so_far < 4) {
        return; // Not enough water yet. Go back to switchboard.
    }
    
    // 6. We have the 4-byte header! Let's read the number inside it.
    uint32_t promised_length = 0;
    memcpy(&promised_length, note.read_bucket, 4);
    
    // 7. Do we have the FULL message yet? (Header + Promised Length)
    if (note.bytes_scooped_so_far < 4 + promised_length) {
        return; // We have the header, but not the full word. Go back to switchboard.
    }
    
    // 8. WE HAVE THE FULL MESSAGE! 
    cout << "Message perfectly received! Length: " << promised_length << endl;
    
    // 9. Prepare our reply
    char my_reply[] = "Hello from Plain English!";
    uint32_t reply_length = strlen(my_reply);
    
    // 10. Fill our writing bucket (Header first, then the word)
    memcpy(note.write_bucket, &reply_length, 4);
    memcpy(&note.write_bucket[4], my_reply, reply_length);
    note.total_bytes_to_send = 4 + reply_length;
    note.bytes_successfully_sent = 0; 
    
    // 11. Change our state! We are done reading, now we need to write.
    note.current_state = STATE_WRITING_RESPONSE;
}

// RULE 2: What to do when trying to WRITE water back
void handle_writing_response(StickyNote& note) {
    // 1. Try to push whatever is in our writing bucket into the pipe
    ssize_t water_pushed = write(note.client_phone_id, 
                                 &note.write_bucket[note.bytes_successfully_sent], 
                                 note.total_bytes_to_send - note.bytes_successfully_sent);
                               
    // 2. Was the pipe too full to accept our water? (Internet lag)
    if (water_pushed < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return; // Pipe full. Try again later.
    }
    
    // 3. Did the client hang up?
    if (water_pushed <= 0) {
        note.current_state = STATE_HANG_UP;
        return;
    }
    
    // 4. Write down how much we successfully pushed
    note.bytes_successfully_sent += (size_t)water_pushed;
    
    // 5. Did we successfully send EVERYTHING?
    if (note.bytes_successfully_sent == note.total_bytes_to_send) {
        // We are done! Wipe the sticky note clean and get ready for their next request.
        note.current_state = STATE_READING_REQUEST;
        note.bytes_scooped_so_far = 0;
        note.total_bytes_to_send = 0;
        note.bytes_successfully_sent = 0;
    }
}

// =====================================================================
// PART 3: THE MAIN SERVER (The Switchboard)
// =====================================================================

int main() {
    // 1. Buy the main server phone and plug it into Port 1234
    int main_server_phone = socket(AF_INET, SOCK_STREAM, 0);
    int val = 1;
    setsockopt(main_server_phone, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = INADDR_ANY;

    ::bind(main_server_phone, (struct sockaddr *)&addr, sizeof(addr));
    listen(main_server_phone, 5000);
    
    // UPGRADE MAIN PHONE TO NON-BLOCKING!
    upgrade_phone_to_nonblocking(main_server_phone);
    
    cout << "Plain English Server listening on Port 1234!" << endl;

    // A filing cabinet to hold all the sticky notes for all active customers
    vector<StickyNote> all_sticky_notes;

    // THE INFINITE EMPLOYEE SHIFT
    while (true) {
        
        // Step A: Build a brand new switchboard for this specific moment
        vector<struct pollfd> switchboard;
        switchboard.push_back({main_server_phone, POLLIN, 0});
        
        // Add all active customers to the switchboard
        for (StickyNote& note : all_sticky_notes) {
            if (note.client_phone_id == -1) continue; // Skip blank notes
            
            struct pollfd pfd = {};
            pfd.fd = note.client_phone_id;
            // If we want to read, wait for POLLIN. If we want to write, wait for POLLOUT.
            pfd.events = (note.current_state == STATE_READING_REQUEST) ? POLLIN : POLLOUT;
            switchboard.push_back(pfd);
        }

        // Step B: Wait for a red light to flash!
        poll(switchboard.data(), switchboard.size(), -1);

        // Step C: Did the MAIN phone flash? (A new customer is calling!)
        if (switchboard[0].revents & POLLIN) {
            int new_customer_phone = accept(main_server_phone, nullptr, nullptr);
            if (new_customer_phone >= 0) {
                // Upgrade their phone so they can never freeze us
                upgrade_phone_to_nonblocking(new_customer_phone);
                
                // Create a brand new sticky note for them!
                StickyNote new_note = {};
                new_note.client_phone_id = new_customer_phone;
                new_note.current_state = STATE_READING_REQUEST;
                all_sticky_notes.push_back(new_note);
            }
        }

        // Step D: Did any CUSTOMER phones flash?
        for (size_t i = 1; i < switchboard.size(); i++) {
            if (switchboard[i].revents) {
                int flashing_phone_id = switchboard[i].fd;
                
                // 1. Find their sticky note in the filing cabinet
                StickyNote* current_note = nullptr;
                for (StickyNote& note : all_sticky_notes) {
                    if (note.client_phone_id == flashing_phone_id) {
                        current_note = &note;
                        break;
                    }
                }
                
                if (!current_note) continue;

                // 2. Read their sticky note to see what we should do
                if (current_note->current_state == STATE_READING_REQUEST) {
                    handle_reading_request(*current_note);
                } 
                else if (current_note->current_state == STATE_WRITING_RESPONSE) {
                    handle_writing_response(*current_note);
                }
                
                // 3. Did they hang up?
                if (current_note->current_state == STATE_HANG_UP) {
                    close(current_note->client_phone_id);
                    // Erase the phone ID so we know this note is blank and can be reused
                    current_note->client_phone_id = -1;
                }
            }
        }
    }
    return 0;
}
