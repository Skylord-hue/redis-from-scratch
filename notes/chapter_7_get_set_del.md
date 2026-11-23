# Chapter 7: The Key-Value Engine (GET, SET, DEL)

## The Prologue: Moving Beyond the Echo Server
**Why did we start?** In Chapters 1 to 6, we built a rock-solid, non-blocking networking engine. It used an event loop (`poll`), handled `EAGAIN`, and avoided blocking when reading fragmented TCP streams.
**The Problem:** Our server was just an echo bot! It took raw strings and shouted back fixed replies. It had zero database capabilities.
**The Solution:** We turn our networking engine into a true **in-memory Key-Value database** supporting commands with multiple arguments:
* `SET key value`
* `GET key`
* `DEL key`
* `KEYS`

---

## 1. The Wire Protocol (Array of Length-Prefixed Strings)

To prevent argument collisions (e.g. what if a value has spaces like `"hello world"`?), we serialize commands as an array of length-prefixed strings.

### Request Format:
```
+---------------+---------------+---------------+--------------+---------------+---------------+
| total_len (4) |  n_args (4)   |  len_1 (4)    |   "SET"      |  len_2 (4)    |    "name"     | ...
+---------------+---------------+---------------+--------------+---------------+---------------+
```

1. **Outer Envelope (4 bytes):** `total_len` of the entire payload.
2. **Argument Count (4 bytes):** `n_args` (number of words in the command).
3. **Each Argument:**
   * 4 bytes: `len`
   * `len` bytes: raw data

---

## 2. Response Format (Status Codes)

When the server responds, it prepends a status code:
```
+---------------+---------------+-----------------------------------+
| total_len (4) |  rescode (4)  |  Response data / Error string     |
+---------------+---------------+-----------------------------------+
```

### Result Codes:
* `RES_OK = 0`: Command succeeded (returns value, `"OK"`, or `"(integer) 1"`).
* `RES_ERR = 1`: Syntax error or unknown command (returns error description).
* `RES_NX = 2`: Key not found (returns `"(nil)"`).

---

## 3. Architecture Workflow

```mermaid
flowchart TD
    A["Raw Bytes in read_buf"] --> B["<b>parse_req()</b><br>Extracts vector&lt;string&gt; cmd"]
    B --> C{"Check cmd[0]<br>(The Router)"}
    
    C -->|cmd[0] == 'get'| D["<b>do_get()</b><br>Find in g_map<br>Return value or RES_NX"]
    C -->|cmd[0] == 'set'| E["<b>do_set()</b><br>Insert into g_map<br>Return RES_OK + 'OK'"]
    C -->|cmd[0] == 'del'| F["<b>do_del()</b><br>Erase from g_map<br>Return RES_OK + '1' or '0'"]
    C -->|cmd[0] == 'keys'| G["<b>do_keys()</b><br>Iterate g_map<br>Return all keys"]
    C -->|Unknown| H["<b>Error</b><br>RES_ERR: unknown cmd"]
    
    D --> I["<b>Package Response</b><br>write_buf: [len][rescode][data]"]
    E --> I
    F --> I
    G --> I
    H --> I
    
    I --> J["conn.state = STATE_RES<br>POLLOUT drains buffer to client"]
```

---

## 4. The Bridge to Chapter 8
In Chapter 7, we use `std::map<string, string> g_map` to establish our command handling and serialization cleanly.
In **Chapter 8**, we replace `std::map` with our own hand-rolled **Hash Table** built from raw pointers with progressive rehashing!
