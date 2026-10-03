# Socket Programming Basics

Here is a visual map of how the 4 core socket functions connect to each other to create a server.

```mermaid
flowchart TD
    A([1. socket<br>Buy the Phone]) -->|We have a device!| B([2. bind<br>Get a Phone Number])
    B -->|We have an address!| C([3. listen<br>Turn on the Ringer])
    C -->|Waiting for calls...| D([4. accept<br>Pick up the Call])
    
    D -->|Creates a NEW connection| E[[Talk to Client 1]]
    D -.->|Ready for the next...| C
    
    style A fill:#ff9999,stroke:#333,stroke-width:2px
    style B fill:#ffcc66,stroke:#333,stroke-width:2px
    style C fill:#99ff99,stroke:#333,stroke-width:2px
    style D fill:#99ccff,stroke:#333,stroke-width:2px
    style E fill:#e6e6fa,stroke:#333,stroke-width:2px
```

### What's going on here?
- Everything flows from top to bottom when you start your server.
- Once you reach **`accept()`**, the server pauses and waits.
- When a client connects, `accept()` spits out a brand new connection just for that client, while the main phone goes back to listening for more calls!
