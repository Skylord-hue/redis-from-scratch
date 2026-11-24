# Chapter 8: The Custom Hash Table (From Scratch in Raw Memory)

## The Prologue: Deleting `std::map`
In Chapter 7, we built the command parsing and response encoding layer, but we used the C++ standard library (`std::map`).
* `std::map` is a **Red-Black Tree** (Binary Search Tree). 
* Lookups take **O(log N)** time.
* If Redis has 10,000,000 keys, every single command requires ~24 pointer hops down the tree.

In **Chapter 8**, we build our own **Hash Table from raw pointers** with **O(1) average time complexity**.

---

## 1. The Core Architecture

The Hash Table is made of three distinct concepts:

### 1. The Array of Pointers (`Entry** buckets`)
The warehouse has a fixed number of drawers (initial capacity = 16).
In memory, this is an array of pointers:
```cpp
Entry* buckets[16] = {nullptr};
```
Accessing any bucket index (e.g. `buckets[7]`) takes **1 CPU clock cycle** via memory multiplication:
$$\text{Address} = \text{Base} + (\text{index} \times 8 \text{ bytes})$$

### 2. The Formula (FNV-1a Hash Function)
Scrambles any string into a 64-bit unsigned integer using XOR and prime multiplication:
```cpp
uint64_t hash = 14695981039346656037ULL;
for (char c : key) {
    hash ^= (uint8_t)c;
    hash *= 1099511628211ULL;
}
```
The bucket index is calculated as:
$$\text{index} = \text{hash} \% \text{capacity}$$

### 3. Collision Resolution via Chaining (Singly-Linked List)
When two completely different keys produce the same bucket index (a Hash Collision):
* We do **not** overwrite the existing key.
* We attach the new node to the front of that bucket's linked list:
```
Bucket[4] ──▶ [key: "cat", val: "meow", next: ──▶] ──▶ [key: "dog", val: "bark", next: nullptr]
```

---

## 2. Operations Overview

* **`get(key)`**: Computes bucket index → walks the short chain → returns value if key matches.
* **`set(key, val)`**: Computes bucket index → if key exists, updates `val` in place. If new, prepends `new Entry` to the bucket list.
* **`del(key)`**: Computes bucket index → unlinks matching node from chain → calls `delete` to free heap RAM.
* **`keys()`**: Iterates through all 16 buckets and prints all active keys.

---

## 3. The Big Picture (Network vs Data)

* **`struct Conn` (The Telephone Line):** Temporary scratchpad for each connected socket. Destroyed when the client disconnects.
* **`struct CustomHashTable g_table` (The Central Warehouse):** One single global instance shared by all clients. Holds all keys and values permanently.
