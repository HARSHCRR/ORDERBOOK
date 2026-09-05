# Low-Latency Order Matching Engine

A high-performance, single-threaded exchange matching engine written in C++ implementing **price-time priority (FIFO)**. This project explores the design, implementation, and performance characteristics of a limit order book — the core data structure behind modern exchanges and HFT systems.

## Performance Highlights

| Metric | Value |
|--------|-------|
| Throughput | **15.33 million orders/sec** |
| Latency | **65.23 ns/order** |
| Scaling | Linear (no degradation at 100M orders) |

---

## Features

### Order Types
- **Limit Buy** — resting buy order at a specified price
- **Limit Sell** — resting sell order at a specified price
- **Market Buy** — immediate execution against best available asks
- **Market Sell** — immediate execution against best available bids

### Matching Logic
- Price-time priority (FIFO)
- Partial fills
- Full fills
- Multiple fills against multiple resting orders

### Order Management
- Unique order IDs (auto-incrementing)
- Order cancellation support
- Iterator-based O(1)-style cancellation

### Market Information
- Best Bid
- Best Ask
- Spread

### Trade Management
- Trade history recording
- Execution reporting

---

## Architecture

### Data Structures

```cpp
map<int, list<Order>, greater<int>> bids;   // Price levels sorted descending
map<int, list<Order>> asks;                  // Price levels sorted ascending
unordered_map<int, OrderInfo> orderMap;      // O(1) order lookup for cancellation
vector<Trade> tradeHistory;                  // Execution log
```

### Bid Side

`map<int, list<Order>, greater<int>>` maintains price levels in **descending** order:

```
100 -> [A, B]
 99 -> [C]
 98 -> [D]
```

### Ask Side

`map<int, list<Order>>` maintains price levels in **ascending** order:

```
101 -> [E]
102 -> [F]
105 -> [G]
```

### Price-Time Priority

Orders at the same price level are stored in a `list<Order>` (or `deque<Order>`), enabling FIFO matching:

```
100 -> A → B → C
```

A is executed before B, B before C.

### Cancellation Support

```cpp
unordered_map<int, OrderInfo> orderMap;

struct OrderInfo {
    bool isBuy;
    int price;
    list<Order>::iterator it;  // Direct iterator into the queue
};
```

Stores an iterator directly into the per-level queue, enabling fast cancellation without scanning the entire book.

---

## Complexity Analysis

| Operation | Complexity |
|-----------|-----------|
| Best Bid / Best Ask | O(1) |
| Insert Order | O(log N) |
| Match Order | O(log N) |
| Cancel Order | O(log N) |
| FIFO Access | O(1) |

Where N = number of distinct price levels in the book.

---

## Implementations

This project includes two implementations to compare backing store performance:

| File | Queue Type | Notes |
|------|-----------|-------|
| `list<>.cpp` | `std::list<Order>` | Doubly-linked list; stable iterators; heap-allocated nodes |
| `deque<>.cpp` | `std::deque<Order>` | Chunked array; better cache locality; contiguous memory blocks |

---

## Benchmark Setup

Randomized workload with uniform distribution across four order types:

```cpp
for (int i = 0; i < NUM_ORDERS; i++) {
    switch (type) {
        case 0: buy(price, qty);    break;
        case 1: sell(price, qty);   break;
        case 2: marketBuy(qty);     break;
        case 3: marketSell(qty);    break;
    }
}
```

- Price range: `uniform_int_distribution<int>(95, 105)`
- Quantity range: `uniform_int_distribution<int>(1, 100)`
- All logging and printing disabled during benchmarks
- Compiled with `-O2` optimization

---

## Benchmark Results

### Baseline Performance

| Workload | Time | Throughput | Latency |
|----------|------|-----------|---------|
| 10M orders | 4.01s | 2.49M orders/sec | 400.9 ns/order |
| 100M orders | 40.19s | 2.49M orders/sec | — |

Throughput remained nearly identical across 10x workload increase, confirming **linear scaling** with no degradation.

### Optimized Baseline (logging disabled)

| Metric | Value |
|--------|-------|
| Throughput | **15.33M orders/sec** |
| Latency | **65.23 ns/order** |

---

## Experiments & Analysis

### Experiment 1 — Cost of Cancellation Bookkeeping

Disabled all `orderMap` insertions and erasures to isolate overhead.

| Configuration | Throughput | Latency |
|---------------|-----------|---------|
| Baseline | 15.33M/s | 65.23 ns |
| Cancellation Disabled | 18.63M/s | 53.69 ns |

**Observation:** Removing cancellation bookkeeping yields a **~21% performance improvement**. The `unordered_map<int, OrderInfo>` is the single largest contributor to runtime overhead.

### Experiment 2 — Hash Map Reservation

```cpp
OrderBook() {
    orderMap.reserve(20000000);
}
```

| Configuration | Throughput | Latency |
|---------------|-----------|---------|
| Baseline | 15.33M/s | 65.23 ns |
| reserve(20M) | 15.62M/s | 64.01 ns |

**Observation:** Minor improvement. Rehashing is not a dominant bottleneck.

### Experiment 3 — Effect of Price Range

| Price Range | Throughput | Latency |
|-------------|-----------|---------|
| 1,000 – 2,000 | 15.76M/s | 63.45 ns |
| 10,000 – 20,000 | 13.72M/s | 72.88 ns |
| 1 – 100,000 | 14.77M/s | 67.70 ns |

**Observation:** More price levels → deeper red-black trees → worse cache locality → more pointer chasing → lower throughput.

---

## Performance Summary

| Experiment | Throughput | Latency |
|-----------|-----------|---------|
| Baseline | 15.33M/s | 65.23 ns |
| Cancellation Disabled | 18.63M/s | 53.69 ns |
| reserve(20M) | 15.62M/s | 64.01 ns |
| Price Range 1K–2K | 15.76M/s | 63.45 ns |
| Price Range 10K–20K | 13.72M/s | 72.88 ns |
| Price Range 1–100K | 14.77M/s | 67.70 ns |

---

## Bottlenecks Identified

### 1. Cancellation Bookkeeping
`unordered_map<int, OrderInfo>` — largest source of overhead due to hashing, memory indirection, and cache pollution.

### 2. Red-Black Trees
`map<int, list<Order>>` introduces:
- O(log N) operations per insert/lookup
- Pointer chasing through tree nodes
- Poor cache locality

### 3. Linked Lists
`list<Order>` causes:
- Per-node heap allocations
- Cache misses on traversal
- Scattered memory access patterns

---

## Build & Run

```bash
# Compile with optimizations
g++ -O2 -o orderbook_list "list<>.cpp"
g++ -O2 -o orderbook_deque "deque<>.cpp"

# Run benchmarks
./orderbook_list
./orderbook_deque
```

---

## Next Steps

- Replace `list<Order>` with `deque<Order>` for better cache locality ✅
- Replace `map<int, list<Order>>` with `vector<deque<Order>>` for O(1) price-level access
- Introduce multi-threading:
  - Mutexes
  - Condition variables
  - Lock-free producer-consumer queues
- Evolve into a low-latency, multi-threaded matching system

---

## Key Takeaway

This project marks the transition from a **data-structures exercise** to a **systems engineering project**. Beyond correctness, the focus is on:

- Latency
- Throughput
- Cache efficiency
- Memory layout
- Low-latency design patterns

These are the fundamental concepts behind modern exchange infrastructure and high-frequency trading systems.

---

## Tech Stack

- **Language:** C++17
- **Containers:** `std::map`, `std::list`, `std::deque`, `std::unordered_map`, `std::vector`
- **Benchmarking:** `std::chrono::high_resolution_clock`
- **RNG:** `std::mt19937` with uniform distributions

