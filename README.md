# Low-Latency Trading Exchange

A full-stack trading exchange built around a **C++17 price-time-priority matching engine**.

The engine is the original project and remains the latency-sensitive core: it owns
matching, the order book, order ids, cancellations and trades. Everything added around
it — a FastAPI orchestration layer and a React dashboard — is deliberately thin.

```
                        React Frontend  (TypeScript, Vite, Tailwind)
                               |
                        HTTP  /  WebSocket
                               |
                               v
                        FastAPI Backend  (orchestration only)
                               |
                        Engine Bridge  (newline-delimited JSON over a pipe)
                               |
                               v
                        C++ Matching Engine  (single-threaded, source of truth)
```

> **On performance claims.** The **15.33M orders/sec / 65.23 ns per order** figures on
> this page belong to the **C++ engine benchmarked in isolation**, in-process, with no
> I/O. The full-stack application does **not** run at that rate — a single HTTP request
> costs roughly **0.8 ms** end-to-end on localhost. Both numbers are measured and both
> are reported below, separately and on purpose. See
> [Performance](#16-performance-results).

---

## Table of contents

1. [What this project is](#1-what-this-project-is)
2. [System architecture](#2-system-architecture)
3. [The C++ matching engine](#3-the-c-matching-engine)
4. [Data structures](#4-data-structures)
5. [Matching algorithm](#5-matching-algorithm)
6. [Cancellation design](#6-cancellation-design)
7. [The engine bridge](#7-the-engine-bridge)
8. [FastAPI API layer](#8-fastapi-api-layer)
9. [WebSocket architecture](#9-websocket-architecture)
10. [React frontend](#10-react-frontend)
11. [Example order flow](#11-example-order-flow-end-to-end)
12. [Local setup](#12-local-setup)
13. [Running everything](#13-running-everything)
14. [Testing](#14-testing)
15. [Benchmarking](#15-benchmarking)
16. [Performance results](#16-performance-results)
17. [What was preserved, what was added](#17-what-was-preserved-what-was-added)
18. [Design decisions and trade-offs](#18-design-decisions-and-trade-offs)
19. [Known limitations](#19-known-limitations)

Also: **[docs/INTERVIEW_GUIDE.md](docs/INTERVIEW_GUIDE.md)** — the design decisions,
trade-offs and failure modes worth being able to defend out loud.

---

## 1. What this project is

A working exchange, in miniature. You can place limit and market orders on both sides,
watch them rest on a live order book, see them match under strict price-time priority,
cancel resting orders, and watch the book and trade tape update in real time in a
browser without polling.

The project started as a data-structures exercise in C++ — a limit order book with a
benchmark harness. The full-stack layer exists to make that engine *observable and
drivable*, not to replace it. Consequently:

* the engine's matching loops were **copied verbatim**, not rewritten;
* the API layer keeps **no state of its own** — no cached book, no order table;
* the original benchmark still builds and runs from the original untouched source file.

---

## 2. System architecture

Four layers, each with one job.

| Layer | Technology | Responsibility | Owns state? |
|---|---|---|---|
| Frontend | React 19, TypeScript, Vite, Tailwind v4 | Render, and submit orders | Only the last execution report shown to *this* browser |
| Backend | FastAPI, Uvicorn | Validate, route, serialise access, broadcast | **No** |
| Bridge | `asyncio.subprocess` + NDJSON | One command in, one response out | Only the subprocess handle and a lock |
| Engine | C++17, STL | Matching, book, ids, trades, cancellation | **Yes — everything** |

The single most important property: **the C++ engine is the only source of truth.**
If `server/main.py` ever starts caching the book, that property is gone and most of the
reasoning below stops holding.

---

## 3. The C++ matching engine

Single-threaded, integer prices and quantities, strict **price-time priority (FIFO)**.

Supported operations:

| Operation | Method | Behaviour |
|---|---|---|
| Limit buy | `buy(price, qty)` | Match against asks at ≤ price, rest the remainder |
| Limit sell | `sell(price, qty)` | Match against bids at ≥ price, rest the remainder |
| Market buy | `marketBuy(qty)` | Consume best asks, **never rests** |
| Market sell | `marketSell(qty)` | Consume best bids, **never rests** |
| Cancel | `cancelOrder(id)` | Remove a resting order via a stored iterator |
| Market data | `getBestBid()` / `getBestAsk()` / `getSpread()` | O(1) reads |

Why single-threaded? A matching engine has one canonical order of events — that
ordering *is* the product. Real exchanges run matching on a single thread per symbol
for exactly this reason. Parallelising it would mean either locking everything (which
serialises it anyway, see `multithreading.cpp`) or accepting non-determinism.

---

## 4. Data structures

```cpp
std::map<int, std::list<Order>, std::greater<int>> bids;  // price levels, descending
std::map<int, std::list<Order>>                    asks;  // price levels, ascending
std::unordered_map<int, OrderInfo>                 orderMap;  // id -> location
std::vector<Trade>                                 tradeHistory;
```

**The two `map`s give price priority for free.** Because `bids` is ordered with
`greater<int>` and `asks` with the default `less<int>`, `begin()` is the best price on
*both* sides. Every matching loop starts at `begin()`; there is no search.

**Each `list` gives time priority for free.** New orders at a price go to the back
(`push_back`), matching consumes from the front (`front()` / `pop_front()`). FIFO
falls out of the container choice.

```
bids                      asks
100 -> [A, B]             101 -> [E]
 99 -> [C]                102 -> [F]
 98 -> [D]                105 -> [G]
 ^                         ^
 begin() = best bid        begin() = best ask
```

**`orderMap` makes cancellation cheap.** It maps an order id to
`{ isBuy, price, list<Order>::iterator }`. `std::list` iterators stay valid when other
elements are inserted or erased, which is precisely why the per-level container is a
list rather than a `vector` or `deque`.

### Complexity

| Operation | Complexity | Why |
|---|---|---|
| Best bid / ask | O(1) | `map::begin()` |
| Insert a resting order | O(log L) | one map lookup, `list::push_back` is O(1) |
| Match one resting order | O(1) amortised | `front()` + `pop_front()` |
| Match a whole order | O(k + log L) | k = resting orders consumed |
| Cancel | O(1) hash + O(1) erase (+ O(log L) if the level empties) | stored iterator |

L = distinct price levels. Note the cost is in the number of *price levels*, not the
number of orders — a book with a million orders across ten prices is as cheap as ten
orders across ten prices.

---

## 5. Matching algorithm

A limit buy, in full:

```cpp
int buy(int price, int qty) {
    int buyId = nextId++;

    while (qty > 0 && !asks.empty() && asks.begin()->first <= price) {
        auto askIt = asks.begin();              // best (lowest) ask
        Order &sellOrder = askIt->second.front(); // oldest at that price

        int traded = std::min(qty, sellOrder.quantity);
        recordTrade(buyId, sellOrder.id, askIt->first, traded);

        qty -= traded;
        sellOrder.quantity -= traded;

        if (sellOrder.quantity == 0) {          // resting order exhausted
            orderMap.erase(sellOrder.id);
            askIt->second.pop_front();
            if (askIt->second.empty()) asks.erase(askIt);
        }
    }

    if (qty > 0) {                              // remainder rests
        bids[price].push_back({buyId, price, qty});
        orderMap[buyId] = {true, price, std::prev(bids[price].end())};
        return buyId;
    }
    return -1;                                  // fully filled, never rested
}
```

Three things to notice:

1. **The trade prints at the *resting* order's price**, not the incoming order's. A buy
   at 105 hitting an ask resting at 101 trades at 101. The resting order set the price;
   the aggressor gets price improvement. This is standard exchange behaviour.
2. **The loop naturally walks price levels.** Exhausting a level erases it from the map,
   so the next iteration's `begin()` is the next-best price. Sweeping multiple levels
   needs no extra code.
3. **`-1` on a full fill.** If the order never rests there is nothing to cancel later, so
   the original engine returns `-1` rather than an id. The bridge works around this
   without changing the contract — see [§7](#7-the-engine-bridge).

Market orders are the same loop with the price condition removed: they take whatever is
there and **discard any unfilled remainder** rather than resting it.

---

## 6. Cancellation design

The naive approach — scan the book for the id — is O(total orders). Instead:

```cpp
struct OrderInfo {
    bool isBuy;
    int  price;
    std::list<Order>::iterator it;   // points directly at the order
};
```

```cpp
bool cancelOrder(int id) {
    auto found = orderMap.find(id);          // O(1)
    if (found == orderMap.end()) return false;

    OrderInfo info = found->second;
    auto &book  = info.isBuy ? bids : asks;
    auto  level = book.find(info.price);     // O(log L)

    level->second.erase(info.it);            // O(1) -- no scan
    if (level->second.empty()) book.erase(level);

    orderMap.erase(id);
    return true;
}
```

`isBuy` and `price` are stored alongside the iterator because a bare `list` iterator
does not know which list it belongs to — the engine needs both to find the level and to
erase it if it empties.

**This is why per-level containers are `std::list`.** A `vector` would invalidate every
stored iterator on reallocation; `deque` invalidates on insertion at either end. `list`
guarantees an iterator stays valid until *that specific element* is erased. The engine
trades cache locality for iterator stability, and the README's own experiments measure
what that costs — see [§16](#16-performance-results).

`orderMap` holds exactly the **resting** orders, so `orderMap.size()` is the live
"active orders" count, and a filled order disappears from it automatically.

---

## 7. The engine bridge

```
FastAPI  ──{"action":"PLACE_ORDER",...}\n──▶  engine_server (C++)
         ◀──{"order_id":123,"status":...}\n──
```

The bridge spawns **one long-lived `engine_server` process** and speaks
newline-delimited JSON over its stdin/stdout. One line in, one line out, strictly in
order.

### Why a subprocess rather than pybind11 or ctypes?

| | Subprocess + NDJSON | pybind11 / ctypes |
|---|---|---|
| Changes needed to the engine | none | bindings, build system, ABI care |
| Debuggable by hand | **yes** — pipe JSON into it from a terminal | no |
| A C++ crash takes down the web server | no | **yes** |
| Cost per command | ~57 µs (measured) | ~1 µs |

The ~57 µs is irrelevant next to an ~800 µs HTTP request, and the isolation and
debuggability are worth far more here. If this were a co-located trading system rather
than a web app, the answer would flip.

### Concurrency

The bridge holds a single `asyncio.Lock` across each write/read pair. That turns any
number of concurrent HTTP and WebSocket callers into one ordered command stream —
exactly the input the single-threaded engine expects. Two clients racing for the same
resting order get the same result they would have got calling the engine directly in
lock order.

This is a **correctness** decision, not a performance one. It is also why end-to-end
throughput does not improve with concurrency ([§16](#16-performance-results)).

### The three problems the bridge solves

The engine was never designed to be driven remotely. Rather than change it, the bridge
adapts:

| Problem | Solution | Engine change |
|---|---|---|
| `buy()`/`sell()` return `-1` on a full fill, losing the id | Read `peekNextOrderId()` *before* calling — that is the id it will assign | additive accessor |
| `marketBuy()`/`marketSell()` never allocate an id at all | Draw one from the same counter via `reserveOrderId()` | additive accessor |
| No way to know which trades one order produced | Snapshot `tradeHistory.size()` before and after; the engine appends in execution order, so the new tail is exactly this order's fills | none |

### Protocol

**Commands:** `PLACE_ORDER`, `CANCEL_ORDER`, `GET_ORDER`, `GET_ORDER_BOOK`,
`GET_TRADES`, `GET_STATS`, `PING`, `SHUTDOWN`.

Drive it by hand — no Python involved:

```bash
make
./build/engine_server
{"action":"PLACE_ORDER","side":"BUY","type":"LIMIT","price":100,"quantity":10}
{"action":"PLACE_ORDER","side":"SELL","type":"LIMIT","price":100,"quantity":4}
{"action":"GET_ORDER_BOOK"}
```
```json
{"filled_quantity":0,"order_id":1,"status":"RESTING","remaining_quantity":10,...}
{"filled_quantity":4,"order_id":2,"status":"FILLED","trades":[{"seq":0,"buy_id":1,"sell_id":2,"price":100,"quantity":4}],...}
{"bids":[{"price":100,"quantity":6}],"asks":[],"best_bid":100,"best_ask":null,"spread":null}
```

A malformed line yields `{"ok":false,"error":"BAD_JSON",...}` and the process keeps
going with its book intact — a bad client cannot destroy exchange state.

The engine's `-1` "no such price" sentinel is translated to JSON `null` at this boundary,
so no layer above ever sees a magic number.

---

## 8. FastAPI API layer

Orchestration only. It validates input, calls the bridge, maps engine errors to HTTP
status codes, and broadcasts. It computes nothing.

| Method | Path | Purpose |
|---|---|---|
| GET | `/health` | Is the engine process alive? |
| GET | `/book?depth=15` | Aggregated book snapshot |
| GET | `/trades?limit=50` | Most recent trades |
| GET | `/stats` | Engine counters |
| POST | `/orders` | Place an order, returns the execution report |
| DELETE | `/orders/{id}` | Cancel a resting order |
| GET | `/orders/{id}` | Look up a **resting** order |
| WS | `/ws` | Real-time feed |

Interactive docs at `http://localhost:8000/docs`.

### Examples

```bash
# Limit buy
curl -X POST localhost:8000/orders -H 'Content-Type: application/json' \
  -d '{"side":"BUY","type":"LIMIT","price":100,"quantity":10}'
```
```json
{"order_id":1,"side":"BUY","type":"LIMIT","price":100,"requested_quantity":10,
 "filled_quantity":0,"remaining_quantity":10,"resting":true,"status":"RESTING","trades":[]}
```

```bash
# Crossing sell -> a trade
curl -X POST localhost:8000/orders -H 'Content-Type: application/json' \
  -d '{"side":"SELL","type":"LIMIT","price":100,"quantity":4}'
```
```json
{"order_id":2,"side":"SELL","type":"LIMIT","price":100,"requested_quantity":4,
 "filled_quantity":4,"remaining_quantity":0,"resting":false,"status":"FILLED",
 "trades":[{"seq":0,"buy_id":1,"sell_id":2,"price":100,"quantity":4}]}
```

```bash
curl -X POST localhost:8000/orders -H 'Content-Type: application/json' \
  -d '{"side":"BUY","type":"MARKET","quantity":25}'   # market: no price field
curl localhost:8000/book
curl localhost:8000/stats
curl -X DELETE localhost:8000/orders/1
```

### Order status

| Status | Meaning |
|---|---|
| `FILLED` | Entirely matched |
| `PARTIALLY_FILLED` | Some matched; remainder rests (limit) or is discarded (market) |
| `RESTING` | Nothing matched; sitting on the book |
| `REJECTED` | Market order into an empty book — no liquidity |

### Status codes

| Code | When |
|---|---|
| 201 | Order accepted (**including** `REJECTED` — the engine ran, it just found nothing) |
| 404 | `GET`/`DELETE` on an order that is not resting |
| 422 | Pydantic rejected the request before it reached the engine |
| 503 | Engine process is down |

A `404` on `GET /orders/{id}` means *"not currently resting"*, not *"never existed"* —
the engine drops filled and cancelled orders from `orderMap` by design and keeps no
archive.

---

## 9. WebSocket architecture

A set of connections and a `broadcast()` that writes the same JSON to each. No broker,
no per-client subscriptions, no message queue — the engine is a single writer and every
client wants every event.

On connect the client gets one `SNAPSHOT` so it can render immediately; after that,
deltas only.

```jsonc
// on connect
{"event":"SNAPSHOT","book":{...},"trades":[...],"stats":{...}}

// after any state change
{"event":"TRADE","trade":{"seq":0,"buy_id":1,"sell_id":2,"price":100,"quantity":4}}
{"event":"BOOK_UPDATE","book":{"bids":[...],"asks":[...],"best_bid":100,"best_ask":101,"spread":1}}
{"event":"STATS","stats":{"orders_processed":2,"trades_executed":1,"active_orders":1,...}}
```

Ordering is deliberate: **`TRADE` events fire before the `BOOK_UPDATE` that reflects
them**, so a client can never show a book that has already absorbed a trade it has not
been told about.

Two details worth knowing:

* **Broadcast is best-effort.** A client that has gone away is dropped, not retried. A
  dead browser tab must never be able to stall the engine.
* **With zero listeners, broadcasting is skipped entirely.** Building a broadcast costs
  two extra engine round-trips, each holding the bridge lock that real orders queue
  behind. Doing that for nobody would triple the cost of an HTTP order.

---

## 10. React frontend

```
src/
├── components/
│   ├── OrderBook.tsx         asks above, spread, bids below; depth bars
│   ├── OrderForm.tsx         BUY/SELL, LIMIT/MARKET, price, quantity
│   ├── TradeHistory.tsx      the tape, newest first
│   ├── Stats.tsx             engine counters
│   ├── ExecutionReport.tsx   last order's result + cancel button
│   ├── ConnectionStatus.tsx  live / connecting / disconnected
│   └── Panel.tsx             shared card chrome
├── websocket.ts   useExchangeFeed() -- ALL real-time state
├── api.ts         thin fetch wrapper
├── types.ts       wire types
└── App.tsx        layout
```

**There is no polling anywhere.** `useExchangeFeed` owns the book, tape, stats and
connection status; the server pushes and the hook applies. The only state `App` owns is
`lastReport` — the execution result of the order *this* browser submitted, which is
private to this client and never broadcast.

Message handling is intentionally dumb — replace on `BOOK_UPDATE`, prepend on `TRADE`.
The server is the only writer, so the client never merges or reconciles.

The order book is rendered as a conventional ladder: **asks above, bids below, prices
increasing upward**, with the spread in the middle. Depth bars behind each row are
scaled to the largest visible quantity on either side so the two halves stay comparable.

The C++ engine records no timestamps, so the tape is keyed by the engine's own execution
sequence number rather than an invented time.

---

## 11. Example order flow (end to end)

Book: one resting bid, order #1, 10 @ 100. A user submits **SELL LIMIT 100 × 4**.

```
1. React      OrderForm -> POST /api/orders {"side":"SELL","type":"LIMIT","price":100,"quantity":4}
                (Vite proxies /api -> :8000, so the browser sees one origin)

2. FastAPI    PlaceOrderRequest validates: LIMIT needs a price, quantity > 0

3. Bridge     acquires the asyncio.Lock
              records tradeHistory.size() == 0
              reads peekNextOrderId() == 2      <- the id sell() is about to use
              writes {"action":"PLACE_ORDER",...,"id":7}\n to the engine's stdin

4. C++        sell(100, 4):
                bids.begin() = 100, 100 >= 100 -> crosses
                front() = order #1 (FIFO)
                traded = min(4, 10) = 4
                tradeHistory.push_back({buyId:1, sellId:2, price:100, qty:4})
                order #1 quantity 10 -> 6, stays resting
                qty -> 0, loop ends, nothing to rest, returns -1
              derives: filled 4, remaining 0, resting false -> "FILLED"
              writes one JSON line to stdout

5. Bridge     reads the line, releases the lock

6. FastAPI    returns 201 with the execution report
              then broadcasts, in order:
                {"event":"TRADE", ...}
                {"event":"BOOK_UPDATE", "book":{"bids":[{"price":100,"quantity":6}], ...}}
                {"event":"STATS", ...}

7. React      the submitting browser shows the execution report from the HTTP response;
              EVERY connected browser updates its book and tape from the WebSocket
```

The HTTP response and the WebSocket events carry the same facts by two routes: the
response answers *"what happened to my order"*, the broadcast answers *"what happened to
the market"*.

---

## 12. Local setup

**Prerequisites:** a C++17 compiler, Python 3.9+, Node 18+.

```bash
git clone https://github.com/HARSHCRR/ORDERBOOK.git
cd ORDERBOOK

# 1. C++ engine
make                                    # -> build/engine_server, engine_tests, benchmark

# 2. Backend
python3 -m venv .venv
./.venv/bin/pip install -r server/requirements.txt

# 3. Frontend
cd frontend && npm install && cd ..
```

---

## 13. Running everything

Two terminals.

```bash
# Terminal 1 -- backend (spawns the C++ engine itself)
cd server && ../.venv/bin/python -m uvicorn main:app --reload --port 8000
```

```bash
# Terminal 2 -- frontend
cd frontend && npm run dev
```

| | URL |
|---|---|
| Dashboard | http://localhost:5173 |
| API docs | http://localhost:8000/docs |
| Health | http://localhost:8000/health |

You do **not** start the C++ engine yourself — FastAPI's lifespan spawns one
`build/engine_server` on startup and shuts it down on exit. Run `make` first, or the
backend will report `ENGINE_MISSING`.

Vite proxies `/api` and `/ws` to port 8000 (see `frontend/vite.config.ts`), so the
browser only ever talks to one origin in development.

**The order book is in memory and lives and dies with the engine process.** Restarting
the backend gives you an empty book with ids starting at 1. There is no database — that
is a deliberate scope decision, not an oversight.

---

## 14. Testing

```bash
make test                              # C++ matching semantics (71 checks)
./.venv/bin/python -m pytest tests     # API, WebSocket, concurrency, C++ build (49 tests)
cd frontend && npm run build           # TypeScript typecheck + production build
```

| Suite | Count | Covers |
|---|---|---|
| `engine/engine_tests.cpp` | 71 checks | Matching semantics directly against the C++ class |
| `tests/test_api.py` | 27 | Every endpoint over real HTTP against a real engine process |
| `tests/test_websocket.py` | 7 | Snapshot, book updates, trade events, multi-client fan-out |
| `tests/test_concurrency.py` | 5 | Contended bridge access, engine-death handling |
| `tests/test_engine_cpp.py` | 10 | The original sources still build; NDJSON protocol by hand |

The Python tests run against a **real engine subprocess**, not a mock. Each test gets a
fresh one via the FastAPI lifespan, so every test starts with an empty book and ids at 1.

Acceptance checklist, all covered:

| # | Scenario | Test |
|---|---|---|
| 1–3 | Limit buy and sell rest on the book | `test_01`–`test_03` |
| 4–5 | A crossing sell produces a trade | `test_04_05` |
| 6 | Partial fill | `test_06`, `test_06b` |
| 7 | Full fill across levels | `test_07` |
| 8 | FIFO at the same price | `test_08`, `test_08b` |
| 9 | Cancellation | `test_09`–`test_09d` |
| 10 | Market buy | `test_10` |
| 11 | Market sell | `test_11`–`test_11c` |
| 12 | WebSocket book update | `test_12`, `test_12b` |
| 13 | WebSocket trade event | `test_13`–`test_13d` |

---

## 15. Benchmarking

```bash
make bench-orig    # the ORIGINAL, untouched list<>.cpp -- the canonical number
make bench         # the same benchmark against the extracted header
```

Both run 10,000,000 orders with the original parameters: `mt19937` seeded at 42, prices
uniform over 95–105, quantities uniform over 1–100, a uniform four-way mix of limit buy,
limit sell, market buy and market sell, all output disabled, `-O2`.

`make bench` compiles with `-DORDERBOOK_BENCHMARK`, which removes trade recording and
the order counter — reproducing the configuration `list<>.cpp` was measured in, where
the `tradeHistory.push_back` calls were commented out.

---

## 16. Performance results

### 16.1 C++ engine, in isolation

Published figures, and what this machine (Apple Silicon, clang -O2) reproduces:

| | Throughput | Latency |
|---|---|---|
| Published baseline | 15.33M orders/sec | 65.23 ns/order |
| Reproduced, `make bench-orig`, 3 runs | 15.31 / 15.58 / 15.64 M/s | 65.3 / 64.2 / 64.0 ns |

The original numbers reproduce. Note this is a **different machine** from the original
measurements; the agreement is a sanity check, not a controlled comparison.

**Extraction cost.** The engine was moved into a header so three binaries could share
it. Measured cost of that move:

| | run 1 | run 2 | run 3 |
|---|---|---|---|
| Original `list<>.cpp` | 16.12M/s | 16.43M/s | 16.20M/s |
| Extracted `OrderBook.hpp` | 15.74M/s | 16.16M/s | 16.02M/s |

~1.7% apart, while the original alone varies ~1.9% run to run. Within noise; no
meaningful regression. (These runs were taken back-to-back on an otherwise-idle machine
and are higher than the §16.1 table above, which is exactly why single runs should not
be quoted.)

### 16.2 Original experiments

Carried over unchanged from the original engine work; see
[`docs/ORIGINAL_README.md`](docs/ORIGINAL_README.md).

| Experiment | Throughput | Latency |
|---|---|---|
| Baseline | 15.33M/s | 65.23 ns |
| Cancellation bookkeeping disabled | 18.63M/s | 53.69 ns |
| `orderMap.reserve(20M)` | 15.62M/s | 64.01 ns |
| Price range 1,000–2,000 | 15.76M/s | 63.45 ns |
| Price range 10,000–20,000 | 13.72M/s | 72.88 ns |
| Price range 1–100,000 | 14.77M/s | 67.70 ns |

Two findings worth being able to explain:

* **Cancellation bookkeeping costs ~21%.** The `unordered_map<int, OrderInfo>` is the
  single largest contributor to runtime — hashing, indirection and cache pollution on
  every insert and erase. That is the price of O(1) cancellation, and it is a
  *deliberate* trade: an exchange that cannot cancel quickly is not an exchange.
* **More price levels means lower throughput.** Wider price ranges deepen the red-black
  trees, worsening cache locality and increasing pointer chasing.

### 16.3 Full-stack application — a completely different scale

Measured on this machine, localhost, no network:

| Layer | Median | p99 |
|---|---|---|
| C++ engine, in-process | **~65 ns** | — |
| Bridge round-trip (Python ↔ pipe ↔ C++, no HTTP) | **~57 µs** | ~68 µs |
| Full `POST /orders` (HTTP → FastAPI → bridge → engine → back) | **~740 µs** | ~880 µs |

End-to-end throughput:

| Concurrency | Orders/sec |
|---|---|
| 1 | ~1,270 |
| 8 | ~980 |
| 32 | ~500 |
| 128 | ~400 |

**Throughput does not improve with concurrency, and mildly degrades.** That is the
design working as intended: the bridge lock serialises every command to preserve the
engine's ordering guarantees, so extra concurrency adds scheduling overhead and no
parallelism. Fixing this would mean giving up deterministic matching order.

**The honest summary:** the engine is ~11,000× faster than the HTTP path in front of it.
Essentially all end-to-end latency is Python, HTTP framing and process I/O — not
matching. The application is a demonstration and control surface for a fast engine; it
is **not itself** a 15M orders/sec system, and this README does not claim otherwise.

---

## 17. What was preserved, what was added

### Preserved exactly

* All original top-level sources are **byte-for-byte unmodified**: `list<>.cpp`,
  `deque<>.cpp`, `c.cpp`, `level2.cpp`, `testlevel2.cpp`, `multithreading.cpp`.
* `make bench-orig` builds and runs `list<>.cpp` exactly as the original README described.
* The original README is preserved at [`docs/ORIGINAL_README.md`](docs/ORIGINAL_README.md).

### Copied verbatim into `engine/OrderBook.hpp`

The container declarations, the `buy` / `sell` / `marketBuy` / `marketSell` matching
loops, `cancelOrder`, `getBestBid` / `getBestAsk` / `getSpread`, `printBook` /
`printTrades`, and the `-1`-on-full-fill return contract.

### Added — all pure additions, none touching the matching loops

| Addition | Why |
|---|---|
| `recordTrade()` / `bumpProcessed()` behind `ORDERBOOK_BENCHMARK` | The server needs trade history; the benchmark needs the exact measured code path. One macro serves both. |
| `peekNextOrderId()` / `reserveOrderId()` | Recover order ids the original API discards |
| `snapshotBids()` / `snapshotAsks()` / `trades()` / `activeOrderCount()` / `isResting()` / `restingQuantity()` | Serialise state to JSON instead of printing to `cout` |

The `recordTrade` calls sit exactly where `list<>.cpp` had its `tradeHistory.push_back`
lines commented out, and match what `deque<>.cpp` did with them enabled.

### Performance claims that remain valid

| Claim | Status |
|---|---|
| 15.33M orders/sec, 65.23 ns/order, for the C++ engine benchmark | **Valid** — reproduced here |
| Linear scaling to 100M orders | **Valid** — untouched, not re-run |
| Cancellation bookkeeping costs ~21% | **Valid** — untouched |
| `reserve(20M)` and price-range experiments | **Valid** — untouched |
| Any of the above describing the *full-stack application* | **Never claimed.** See §16.3. |

---

## 18. Design decisions and trade-offs

**Why is the API layer stateless?** Because two sources of truth is one too many. Any
cached book would need invalidating on every trade, cancel and partial fill, and the
first bug in that logic would show users a market that does not exist. Asking the engine
costs ~57 µs and is always right.

**Why integer prices?** The engine keys its `map` on `int`. Introducing floats at the
API boundary would mean rounding somewhere, and rounding a price is how you invent
trades that did not happen. There is exactly one price representation in this system.
(A real exchange would use fixed-point minor units — cents or ticks — which is the same
decision.)

**Why no database?** Nothing here needs durability to demonstrate matching, and adding
one would mean either writing through on the hot path (slowing the thing the project is
about) or writing asynchronously (introducing a second source of truth). See
[§19](#19-known-limitations).

**Why is `nlohmann/json` vendored?** It is the one dependency the C++ side has. Hand-
rolling a JSON parser to avoid a header would be a liability, not a simplification; a
single vendored header needs no package manager and no build configuration.

**Why `list` rather than `deque` per price level?** Iterator stability. `deque`
invalidates iterators on insertion, which would break `orderMap`-based cancellation.
`deque<>.cpp` exists to measure what the cache-locality difference is worth.

---

## 19. Known limitations

Stated plainly, because knowing what a system does *not* do is part of understanding it.

* **No persistence.** Restarting the backend empties the book. In-memory by design.
* **No authentication, accounts, positions or risk checks.** Anyone can place any order;
  nobody owns anything.
* **One instrument.** The engine has a single book. Multiple symbols would mean one
  engine instance per symbol — which is how real exchanges shard, and would fit this
  architecture without redesign.
* **No order history.** The engine drops orders from `orderMap` on fill or cancel, so
  `GET /orders/{id}` only answers for *resting* orders.
* **Self-trading is allowed.** There are no accounts, so there is nothing to prevent it.
* **`deque<>.cpp` has no `main()`.** It defines the class and stops. The original
  README's `g++ -O2 -o orderbook_deque "deque<>.cpp"` has never linked. The file is left
  as it was rather than silently altered; `make check-deque` syntax-checks it.
* **`c.cpp`, `level2.cpp`, `testlevel2.cpp` are early iterations** kept for history.
  `level2.cpp` contains real bugs (`qty = -traded` where `qty -= traded` was meant).
  They are not part of the running system and were left untouched.

---

## Tech stack

**Engine** — C++17, `std::map`, `std::list`, `std::unordered_map`, `std::vector`,
`std::chrono`, `nlohmann/json` (vendored).
**Backend** — Python, FastAPI, Uvicorn, Pydantic v2, `asyncio.subprocess`.
**Frontend** — React 19, TypeScript, Vite, Tailwind CSS v4.
**Testing** — pytest, Starlette `TestClient`, a hand-rolled C++ check harness.

No Kafka, no Redis, no database, no Docker, no Kubernetes, no auth. Each of those would
add a component to explain without adding anything the project needs.
