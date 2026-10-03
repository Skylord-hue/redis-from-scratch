# Redis from Scratch (C++)

This repository documents my journey building a Redis clone from scratch in C++. 

Currently, the **Networking Layer** is fully implemented using raw POSIX sockets and a custom single-threaded event loop.

## Features (Chapters 1-6)
- **Single-threaded Event Loop:** Uses `poll()` to serve multiple clients asynchronously without a thread per connection.
- **Non-blocking I/O:** Uses a per-connection state machine (`read request` -> `write response` -> `close`) that gracefully handles `EAGAIN` to prevent the server from freezing on partial reads/writes.
- **Custom Protocol:** Uses a length-prefixed (LV) protocol with a 4-byte header to solve TCP stream fragmentation and perfectly extract messages.

## How to Build and Run
1. Compile the server:
   ```bash
   g++ -Wall -Wextra -O2 -g server.cpp -o server
   ```
2. Run the server:
   ```bash
   ./server
   ```
3. In a separate terminal, test the connection using `nc` (netcat):
   ```bash
   nc 127.0.0.1 1234
   ```

## Next Steps
- Implement the actual Key-Value Store.
- Build a custom Hash Table and AVL Tree from raw memory.
