# Level 2 Order Book – Price-Time Priority Matching Engine

## Overview

This is the Level 2 implementation of a simple stock exchange matching engine written in C++.

The engine supports:

* Limit Buy Orders
* Limit Sell Orders
* Price Priority
* Time Priority (FIFO)
* Partial Fills
* Automatic Matching

The implementation uses:

```cpp
map<int, queue<Order>, greater<int>> bids;
map<int, queue<Order>> asks;
```

which models the price-time priority used by real exchanges.

---

## Data Structures

### Bid Side

```cpp
map<int, queue<Order>, greater<int>> bids;
```

Stores buy orders.

Prices are sorted in descending order:

```
104
103
102
101
```

The best bid is available in:

```cpp
bids.begin()
```

---

### Ask Side

```cpp
map<int, queue<Order>> asks;
```

Stores sell orders.

Prices are sorted in ascending order:

```
100
101
102
103
```

The best ask is available in:

```cpp
asks.begin()
```

---

### Queue<Order>

Orders at the same price are stored inside a queue.

Example:

```
Price 100

Order A
Order B
Order C
```

The queue provides FIFO execution:

```
First In
First Out
```

which gives time priority.

---

## Matching Rules

### Price Priority

Buyers execute against the cheapest available sellers.

Sellers execute against the highest available buyers.

---

### Time Priority

Among orders with the same price, older orders execute before newer orders.

---

### Partial Fill Support

Example:

```
BUY 100 x 10

SELL 100 x 4
```

Trade:

```
4 shares @ 100
```

Remaining book:

```
BUY 100 x 6
```

---

## Complexity

| Operation              | Complexity |
| ---------------------- | ---------- |
| Find best bid          | O(1)       |
| Find best ask          | O(1)       |
| Insert new price level | O(log P)   |
| Queue push             | O(1)       |
| Queue pop              | O(1)       |
| Queue front            | O(1)       |

where:

```
P = number of price levels
```

---

## Benchmark

Compiled using:

```bash
g++ -O2 testlevel2.cpp -o testlevel2
```

### Benchmark Configuration

Buy orders:

```cpp
for(int i = 0; i < 1000000000; i++) {
    ob.addBuy(100 + (i % 5), 1 + (i % 10));
}
```

Sell orders:

```cpp
for(int i = 0; i < 1000000000; i++) {
    ob.addSell(102 + (i % 5), 1 + (i % 10));
}
```

Total orders processed:

```
1,000,000,000 Buy Orders
+
1,000,000,000 Sell Orders
=
2,000,000,000 Orders
```

Trade logging and book printing were disabled during benchmarking.

### Execution Time

```
102,786,595 microseconds
≈ 102.79 seconds
```

### Throughput

```
≈ 19.5 million orders/second
```

---

## Features Implemented

* [x] Buy Orders
* [x] Sell Orders
* [x] Automatic Matching
* [x] Partial Fills
* [x] Price Priority
* [x] FIFO Time Priority

---

## Planned Features

### Level 3

* Order IDs
* Order Cancellation
* O(1) Order Lookup

### Level 4

* Best Bid
* Best Ask
* Spread

### Level 5

* Trade History

### Level 6

* Market Orders

### Level 7

* Performance Optimizations
* Cache-Friendly Structures
* Memory Pool Allocator
* Multi-threading

---

## Architecture

```
               OrderBook
              /         \
             /           \
          Bids           Asks
           |               |
 map<int, queue>    map<int, queue>
           |               |
       Price Priority + FIFO
                  |
                  v
          Price-Time Priority
```
