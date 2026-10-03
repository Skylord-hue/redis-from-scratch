## 🎯 Goal Description
This is your 6-hour study plan for building Redis from scratch in C/C++, following the syllabus of "Build Your Own Redis with C/C++" by James Smith. 

The goal of this plan is to ensure we follow **your specific learning style**:
1. You will be shown the **Technical Definition** first.
2. You will then be shown a **5-year-old Analogy** second.
3. You will review visual diagrams linking the concepts.
4. You will attempt to write the code yourself based on the understanding.
5. I will only write code to review yours or if you get stuck.

---

## 📚 Today's 6-Hour Syllabus (The Topics We Will Cover)

Since you have 6 hours dedicated to this today, we are going to cover the fundamental networking and concurrency chapters of the book. 

### Topic 1: The Socket API (The Foundation)
* **What we will learn:** `socket()`, `bind()`, `listen()`, `accept()`.
* **The Goal:** Build a basic server that can open a port and accept a connection (which you attempted earlier, but we will redo properly with definitions).

### Topic 2: Socket I/O (Talking over the wire)
* **What we will learn:** `read()`, `write()`, `send()`, `recv()`.
* **The Goal:** Make the client and server exchange a "Hello" message. 

### Topic 3: The TCP Byte Stream (The invisible boundaries)
* **What we will learn:** Why TCP is a continuous stream of bytes, not "messages". We will learn about the problem of splitting a byte stream into messages.
* **The Goal:** Understand why our simple `read()` and `write()` from Topic 2 will break if we send too much data or send it too fast.

### Topic 4: Data Serialization & Protocols
* **What we will learn:** How to create a mapping between C++ objects (like a struct or a string) and raw bytes (0s and 1s) to send over the network safely. We will design our custom Redis protocol.
* **The Goal:** Write code that prepends the "length" of a message before the message itself, so the server knows exactly how many bytes to read.

### Topic 5: Concurrent Programming & Event Loops
* **What we will learn:** The C10K problem (Handling 10,000 concurrent connections). Blocking vs. Non-blocking I/O. The `poll()` system call.
* **The Goal:** Rewrite our server using an Event Loop so it can talk to multiple clients at the exact same time without freezing on `accept()` or `read()`.

---

## ❓ Open Questions / User Review Required
1. Does this 5-topic syllabus look like a solid 6-hour session for you?
2. Are you ready to restart **Topic 1**, where I will give you the *Technical Definition* first, then the *Analogy* second, and wait for you to write the code?
