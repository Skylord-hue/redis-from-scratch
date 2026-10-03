# Senior Engineer Interview Prep (Chapters 1 - 5)

This document contains real-world systems architecture questions based on the Redis networking engine. Use these to test your active recall and prepare for deep technical interviews.

---

### Question 1: The Multi-threading Trap
**Question:** Your boss tells you: *"Our new server needs to handle 10,000 customers at once! We should hire 10,000 threads so every customer gets their own thread."* Explain why this is a terrible idea, and how our C++ server uses `poll()` to handle 10,000 customers on a single thread.

**The Polished Answer:**
Hiring 10,000 threads is extremely expensive. Each thread consumes heavy RAM for its memory stack and wastes massive CPU power doing "context switching" (the CPU constantly pausing and resuming different threads). If 10,000 users connect, the server will crash from memory exhaustion. 
Instead, Redis uses a **Single-Threaded Event Loop** (I/O Multiplexing). The single thread acts as a switchboard operator. All active File Descriptors (Phones) are placed into an array. The `poll()` function monitors the entire array at once and only wakes the thread up when a specific FD flashes a "red light" indicating data is ready. The single thread quickly processes that one FD, and then spins back to the switchboard. 

---

### Question 2: The Port Paradox
**Question:** Customer A connects from their laptop. Customer B connects from their phone. Your server is only listening on Port 1234. How is it physically possible for both customers to talk to Port 1234 at the exact same time without their messages getting mixed up?

**The Polished Answer:**
A TCP connection is not just identified by the Server's port. It is identified by a **4-Tuple (The Caller ID)**:
1. Server IP
2. Server Port (1234)
3. Client IP
4. Client Port (A random temporary port assigned by the client's OS).

When the server calls `accept()`, it generates a unique Client FD for that specific 4-tuple. When data arrives at Port 1234, the Operating System secretly looks at the Client IP and Client Port, and uses that "Caller ID" to perfectly route the data to the correct Client FD. 

---

### Question 3: The Memory Illusion
**Question:** When a client sends the word `hello`, the `read()` function scoops it up into a `char buffer[1024]`. Does `read()` know that it is reading the English word "hello"? And when you type `cout << buffer`, how does C++ know exactly when to stop printing if you didn't write a `for` loop?

**The Polished Answer:**
No, `read()` is completely blind. It does not speak English. It just scoops raw binary (1s and 0s) from the network pipe and dumps them into the buffer. The translation only happens because of the ASCII table. 
When we use `cout << buffer`, C++ uses a **hidden `for` loop**. Because `buffer` is a character array, `cout` starts at index 0 and prints every character until it hits the invisible **Null Terminator (`\0`)**. That invisible zero acts as the stop sign.

---

### Question 4: The TCP Water Hose
**Question:** I want to send you two commands: `SET x 1` and `GET x`. Explain why standard TCP would ruin these messages if you just used a normal `read()`. Then, explain exactly how the **Length Header Protocol** and `read_full` solve this.

**The Polished Answer:**
TCP is not a message-based system (envelopes); it is a continuous stream of bytes (a water hose). If you send two commands quickly, they mix together in the pipe into one giant block: `SET x 1GET x`. If the internet lags, a single `read()` might scoop up half a word, destroying the command.
To fix this, we use a **Protocol**. The client promises exactly how many bytes it will send by placing a 4-byte Header at the start (e.g., `[7]SET x 1`). 
The server uses a custom `read_full` function (a `while` loop) that patiently scoops water from the pipe over and over again, advancing its pointer each time, until it collects the exact number of bytes promised in the header. This guarantees messages are never cut in half or merged together.
