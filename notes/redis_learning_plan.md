## 🚀 Goal Description
We are going to build our own version of Redis from scratch in C/C++. The goal is to take the complex concepts from "Build Your Own Redis with C/C++" (like TCP streams, event loops, and serialization) and break them down into simple, visual analogies (like a 5-year-old could understand). 

I do not have the copyrighted book memorized page-to-page, but I know **exactly** what the book teaches and the standard architectural steps to build Redis. We will follow that exact path, but I will serve as your personal, simplified translator.

---

## 🗺️ The "Explain it to a 5-Year-Old" Roadmap

Here is how we will build the system, step-by-step, connecting each piece together.

### Phase 1: The Telephone Line (Socket Programming)
**The Concept:** Before two programs can talk, they need a wire between them. In programming, this is a **Socket**.
**The Analogy:** Imagine two kids with tin cans and a string. 
- **Server (Redis):** One kid sitting in a treehouse, listening to the can (binding to a port and listening).
- **Client (You):** Another kid on the ground, talking into their can.
- We will write the C++ code to set up this string and make sure the server can hear a simple "Hello".

### Phase 2: The Secret Language (Protocols & Serialization)
**The Concept:** The string (TCP) only sends raw sounds (bytes). It doesn't know where a word starts or ends. We need a rule so the server knows when a full message has arrived.
**The Analogy:** Imagine passing a long, continuous strip of paper with letters on it. How do you know where one sentence ends? You agree to put a special sticker (like a length number or a special character) at the start or end of every sentence.
- We will build the **Redis Protocol (RESP)** parser. We'll tell the computer: "Read the first number, that tells you how long the word is. Then read that many letters."

### Phase 3: The Juggling Chef (Event Loops & Concurrency)
**The Concept:** A server needs to talk to 10,000 clients at the same time. If it stops to talk to one, the others wait (blocking). 
**The Analogy:** Imagine a chef in a restaurant. 
- **Bad way (Blocking):** The chef takes an order, cooks it, and brings it out. Everyone else waits.
- **Good way (Event Loop):** The chef takes an order, puts it in the oven. While it cooks, they take another order from someone else. When the oven goes *DING*, they stop and take the food out. 
- We will implement an **Event Loop** (using `poll` or `epoll` in C++) so our server can juggle thousands of connections at once without ever stopping to wait.

### Phase 4: The Giant Post-it Wall (Hash Tables)
**The Concept:** Redis is a Key-Value store. It needs to find data instantly, even if there are millions of items.
**The Analogy:** Imagine a giant wall of mailboxes. If you want to store a toy for "Batman", you run his name through a special math machine (Hash Function). The machine spits out the number "42". You put the toy in mailbox 42. When you want the toy back, you just ask the machine for "Batman", it says "42", and you go straight to that mailbox without searching the whole wall!
- We will build a **Hash Table** from scratch in C++ to store our GET and SET commands.

### Phase 5: The Leaderboard (Z-Sets & AVL Trees)
**The Concept:** Redis can keep sorted lists (like high scores in a game) that update instantly.
**The Analogy:** Imagine a school height chart. Whenever a new kid comes, they instantly know exactly where to stand based on their height.
- We will build an **AVL Tree** (a self-balancing tree), which keeps numbers sorted automatically as you add them.

---

## ❓ Open Questions
1. **Pacing:** Do you want me to give you the code directly to copy-paste, or do you want me to explain the concept and have *you* try writing the code first, and then we review it together?
2. **Environment:** Since you are on a Mac, we will use standard C++ and the `poll` system call for the event loop (since `epoll` is Linux-only). Does that sound good?

---

## 🛠️ Proposed Execution Plan

1. **Setup:** Create a new folder `redis-scratch` in your workspace.
2. **Step 1:** Write `server.cpp` to create a basic socket that listens for a connection.
3. **Step 2:** Write `client.cpp` to connect to it.
4. **Step 3:** Implement message parsing (the secret language).
5. **Step 4:** Implement the Event Loop (the juggling chef) so `server.cpp` can handle multiple `client.cpp`s.
6. **Step 5:** Build the Hash Table and add `GET`/`SET`/`DEL` commands.

Let me know if you approve of this plan, and we will start with Phase 1!
