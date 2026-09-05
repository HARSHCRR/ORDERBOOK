# Interview Guide

A study companion to the [README](../README.md). The README explains what the system
*is*; this explains what to *say* about it, and what to say when someone pushes back.

---

## Part 1 — The 60-second pitch

> "It's a trading exchange built around a C++17 matching engine I'd already written. The
> engine does price-time priority matching on a limit order book — two `std::map`s for
> the price levels, a `std::list` per level for FIFO, and an `unordered_map` from order
> ID to a list iterator so cancellation doesn't scan the book.
>
> I wanted to make it drivable from a browser without compromising it, so I wrapped it
> in a process that speaks newline-delimited JSON over stdin/stdout. A FastAPI layer
> owns that subprocess and serialises access to it behind a single lock — the engine is
> single-threaded on purpose, because a matching engine's ordering guarantee *is* the
> product. React talks to FastAPI over HTTP for actions and a WebSocket for the live
> feed.
>
> The important constraint I set myself was that FastAPI keeps no state at all. The C++
> engine is the only source of truth for the book, order IDs and trades. Every request
> asks it."

Then stop. Let them pick the thread.

---

## Part 2 — Know these cold

### Why two maps with opposite comparators?

`bids` is `map<int, list<Order>, greater<int>>`; `asks` is `map<int, list<Order>>`.
So `begin()` is the **best price on both sides** — highest bid, lowest ask. Every
matching loop starts at `begin()` and there is never a search for the best price.

### Where does time priority come from?

The `std::list` at each price level. New orders `push_back`; matching consumes
`front()` and `pop_front()`. FIFO is a property of the container choice, not of any
sorting code.

### Why `std::list` and not `vector` or `deque`?

**Iterator stability.** `orderMap` stores a `list<Order>::iterator` pointing directly at
each resting order, which is what makes cancellation O(1) instead of a book scan.
`vector` invalidates every iterator on reallocation. `deque` invalidates on insertion at
either end. `list` guarantees an iterator stays valid until that *specific* element is
erased.

The cost is real: heap allocation per node and poor cache locality. `deque<>.cpp` exists
to measure it. **This is the single best trade-off question in the project** — be ready
to say you chose correctness of the cancel path over cache locality, and that you
measured the alternative.

### What is the complexity of each operation?

| Operation | Complexity |
|---|---|
| Best bid / ask | O(1) — `map::begin()` |
| Insert resting | O(log L) |
| Match one resting order | O(1) amortised |
| Match a whole order | O(k + log L), k = orders consumed |
| Cancel | O(1) hash + O(1) erase, + O(log L) if the level empties |

**L is the number of distinct price levels, not orders.** A million orders across ten
prices costs the same as ten orders across ten prices. That's the point of the design.

### At what price does a trade execute?

The **resting** order's price. A buy at 105 hitting an ask resting at 101 trades at 101.
The resting order was there first and set the price; the aggressor gets price
improvement. Standard exchange behaviour, and it falls out of the code naturally —
`recordTrade(..., askIt->first, ...)` uses the map key, not the incoming price.

### What happens to an unfilled market order?

It's discarded, never rested. A market order has no price, so there is no price at which
it could rest. If the book runs dry it's `PARTIALLY_FILLED`; if the book was empty to
begin with it's `REJECTED`.

### Why is the engine single-threaded?

A matching engine has one canonical ordering of events, and that ordering *is* the
product — it's what makes matching fair and reproducible. Real exchanges run one
matching thread per symbol for exactly this reason. Parallelising means either locking
everything (which serialises it anyway — that's what `multithreading.cpp` demonstrates)
or accepting that two runs of the same input produce different fills.

**How would you scale it, then?** Shard by instrument: one engine process per symbol.
Symbols don't interact, so this parallelises perfectly and preserves determinism within
each book. This architecture already supports it — the bridge spawns a process, so it
would spawn N.

---

## Part 3 — Questions you will be asked

**"Why a subprocess instead of pybind11?"**
Three reasons. The engine needed zero changes to be driven this way. A C++ crash can't
take the web server down — the bridge sees EOF and reports it. And the protocol is
newline-delimited JSON, so I can debug the engine by piping text into it from a
terminal. The cost is ~57 µs per command, which is nothing next to an ~800 µs HTTP
request. If this were a co-located trading system rather than a web app I'd have made
the opposite call.

**"Your lock serialises everything. Isn't that a bottleneck?"**
Yes, and deliberately. The engine is single-threaded, so the lock isn't costing
parallelism that existed — it's providing the ordering the engine requires. I measured
it: throughput actually *falls* from ~1,270/sec at concurrency 1 to ~400/sec at
concurrency 128, because extra concurrency adds scheduling overhead and no parallelism.
Removing the lock wouldn't make it faster; it would make it wrong.

**"Why doesn't FastAPI cache the order book?"**
Because two sources of truth is one too many. A cache would need invalidating on every
trade, cancel and partial fill, and the first bug in that logic shows users a market
that doesn't exist. Asking the engine costs ~57 µs and is always right.

**"Your engine does 15 million orders a second — so the app does?"**
No, and I'm careful about this. 15.33M/sec and 65 ns/order are the C++ engine
benchmarked in-process with no I/O. A full HTTP request through the stack is ~740 µs
median — about 11,000× slower. Essentially all of that is Python, HTTP framing and pipe
I/O, not matching. The app is a control surface for a fast engine; it isn't itself a
fast system.

*(This answer earns more credit than the big number does. Say it before you're asked.)*

**"`buy()` returns -1 when the order fills completely. How do you report an order ID?"**
That was the most interesting integration problem. The original engine returns `-1`
because a fully-filled order never rests, so there's nothing to cancel later. Rather
than change the contract, the bridge reads `peekNextOrderId()` *before* calling — that's
the ID the engine is about to assign. Market orders were worse: they never allocated an
ID at all, so I added `reserveOrderId()` to draw one from the same counter. Both are
pure additions; the matching loops are untouched.

**"How do you know which trades a specific order produced?"**
Snapshot `tradeHistory.size()` before the call and after. The engine appends in
execution order, so the new tail is exactly that order's fills. No engine change at all.

**"How would you add persistence?"**
Event sourcing — append every accepted command to a log before executing it. Replaying
the log rebuilds the book exactly, because the engine is deterministic. I would *not*
write the book itself to a database: that's either on the hot path (slow) or async
(second source of truth). The determinism that makes the engine testable is the same
property that makes the log sufficient.

**"How would you handle 100 symbols?"**
One engine process per symbol; the bridge becomes a dict of processes keyed by symbol.
No shared state between books, so it parallelises cleanly. The alternative — one process
with a map of books — reintroduces a single thread as the bottleneck for all symbols.

**"What's the biggest weakness?"**
No persistence — restarting empties the book. After that, the API layer is a genuine
throughput ceiling. If it needed to be fast I'd replace HTTP with a binary protocol over
a Unix socket and drop JSON, which is roughly what real venues do with FIX or a
proprietary binary format.

**"What bug did you actually hit?"**
Two worth telling.

*The `asyncio.Lock` loop binding.* On Python 3.9 a `Lock` binds to whatever event loop
exists when it's constructed, and my bridge was built at module import time — before
uvicorn's loop existed. An uncontended `acquire()` never touches the loop, so it worked
in every test and every manual check. The moment two requests genuinely overlapped it
threw "got Future attached to a different loop". A multi-client WebSocket test caught
it. The fix is to construct the lock inside `start()`, which runs on the serving loop.
The lesson: a lock that's only tested uncontended isn't tested.

*Duplicate WebSocket connections under React StrictMode.* StrictMode mounts effects
twice in development. The cleanup closed socket A and the remount opened socket B, but
A's `onclose` fired asynchronously *after* the remount had cleared the "unmounting"
flag — so A reconnected itself and I had two live sockets writing to the same state.
Every trade appeared twice. The fix is an identity check: only reconnect if
`socketRef.current === socket`. A boolean flag wasn't enough because the race is about
*which* socket, not *whether* we're mounted.

---

## Part 4 — Every component in one paragraph

**`engine/OrderBook.hpp`** — the matching engine, extracted verbatim from `list<>.cpp`
into a header so three binaries can share it. Owns the two price-level maps, the
order-ID lookup map and the trade log. Trade recording and the order counter sit behind
`ORDERBOOK_BENCHMARK` so the benchmark compiles the exact instruction path that was
originally measured.

**`engine/engine_server.cpp`** — a `while (getline(cin, line))` loop that parses one JSON
command, dispatches to the engine, and writes one JSON response. Derives execution
reports from engine state rather than recomputing anything. Bad input returns an error
object and the loop continues, so a broken client can't destroy the book.

**`server/engine_bridge.py`** — the only thing in the codebase that talks to C++. Spawns
one long-lived process, and holds an `asyncio.Lock` across each write/read pair so any
number of concurrent callers become one ordered command stream. Also translates the
engine's `-1` "no such price" sentinel into JSON `null`.

**`server/models.py`** — Pydantic models. Exist to reject bad input before it reaches the
engine and to generate OpenAPI docs. They deliberately do *not* re-model engine state.

**`server/websocket_manager.py`** — a set of connections and a `broadcast()`. Best-effort:
a client that has gone away is dropped, not retried, because a dead browser tab must
never stall the engine.

**`server/main.py`** — the seven endpoints plus `/ws`. Validates, calls the bridge, maps
engine errors to status codes, broadcasts. Computes nothing. Skips broadcasting entirely
when nobody is listening, because building a broadcast costs two extra engine
round-trips holding the lock real orders queue behind.

**`frontend/src/websocket.ts`** — `useExchangeFeed()`, which owns *all* real-time state.
Replace on `BOOK_UPDATE`, prepend on `TRADE`. The server is the only writer so the client
never merges or reconciles. No polling anywhere in the app.

**`frontend/src/App.tsx`** — layout, plus the single piece of local state: `lastReport`,
the execution result of the order *this* browser submitted. Private to this client and
never broadcast — which is exactly the distinction between "what happened to my order"
and "what happened to the market".

---

## Part 5 — Numbers to have memorised

| | |
|---|---|
| Engine throughput (isolated, `-O2`) | 15.33M orders/sec |
| Engine latency | 65.23 ns/order |
| Cancellation bookkeeping overhead | ~21% (18.63M/sec without it) |
| Bridge round-trip | ~57 µs |
| Full HTTP request | ~740 µs median |
| Engine : HTTP ratio | ~1 : 11,000 |
| C++ engine tests | 71 checks |
| Python tests | 49 |

If you remember one thing: **quote the 65 ns and the 740 µs together.** The pair shows
you understand where the boundary is; either alone doesn't.

---

## Part 6 — Things not to claim

* Don't say the application handles 15M orders/sec. It handles roughly 1,200.
* Don't call the cancellation O(1) without qualification — it's O(1) hash lookup and
  O(1) list erase, plus O(log L) if the price level empties.
* Don't say it's "production ready". No persistence, no auth, no risk checks, one
  instrument, self-trading permitted.
* Don't claim you rewrote or optimised the matching engine for this project. You
  preserved it and built around it — which is the more defensible engineering decision
  and a better story.
