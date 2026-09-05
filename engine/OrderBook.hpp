// ---------------------------------------------------------------------------
// OrderBook.hpp
//
// This header is a HEADER-ONLY EXTRACTION of the OrderBook class that already
// lived inside "list<>.cpp" and "deque<>.cpp".  It exists only so that the
// same matching engine can be linked into three different binaries:
//
//     engine_server.cpp  -> the JSON command process used by the FastAPI layer
//     benchmark.cpp      -> the throughput benchmark
//     engine_tests.cpp   -> matching-semantics unit tests
//
// WHAT WAS COPIED VERBATIM (do not "clean up" — this is the measured code):
//     - map<int, list<Order>, greater<int>> bids / map<int, list<Order>> asks
//     - unordered_map<int, OrderInfo> orderMap
//     - vector<Trade> tradeHistory
//     - the buy() / sell() / marketBuy() / marketSell() while-loops
//     - the iterator-based cancelOrder()
//     - getBestBid() / getBestAsk() / getSpread()
//     - buy()/sell() still return -1 when the order fully filled and never
//       rested.  That is the ORIGINAL contract and callers depend on it.
//
// WHAT WAS ADDED (pure additions — none of them touch the matching loops):
//     - trade recording + an order counter sit behind ORDERBOOK_BENCHMARK so the
//       benchmark compiles the exact same instruction path that produced the
//       15.33M orders/sec figure (where the push_back calls were commented
//       out), while the server compiles them in (as deque<>.cpp did).
//     - peekNextOrderId() / reserveOrderId()  -> lets the bridge learn the id
//       the engine is about to assign, and lets MARKET orders draw an id from
//       the same monotonic counter (the original marketBuy/marketSell never
//       allocated one).
//     - snapshotBids() / snapshotAsks() / trades() / activeOrderCount() /
//       ordersProcessed()  -> read-only accessors so the book can be
//       serialised to JSON instead of printed to cout.
//     - printBook() / printTrades() are unchanged.
// ---------------------------------------------------------------------------

#ifndef ORDERBOOK_HPP
#define ORDERBOOK_HPP

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Original structs, unchanged.
// ---------------------------------------------------------------------------
struct Order {
    int id;
    int price;
    int quantity;
};

struct Trade {
    int buyId;
    int sellId;
    int price;
    int quantity;
};

struct OrderInfo {
    bool isBuy;
    int price;
    std::list<Order>::iterator it;
};

// ADDED: aggregated view of one price level, for JSON serialisation only.
struct BookLevel {
    int price;
    int quantity;
};

class OrderBook {

private:

    // ---- original data structures, unchanged --------------------------------
    std::map<int, std::list<Order>, std::greater<int>> bids;
    std::map<int, std::list<Order>> asks;

    std::unordered_map<int, OrderInfo> orderMap;

    std::vector<Trade> tradeHistory;

    int nextId = 1;

    // ADDED: pure counter, never read by the matching loops.
    long long processedCount = 0;

    // ADDED: single funnel for trade recording, and for the processed-order
    // counter.  When ORDERBOOK_BENCHMARK is defined BOTH compile to nothing,
    // so the benchmark binary executes exactly the instruction path that
    // list<>.cpp was measured in (where the push_back calls were commented
    // out and no counter existed).  The server compiles them in, as
    // deque<>.cpp did.
    inline void recordTrade(int buyId, int sellId, int price, int quantity) {
#ifndef ORDERBOOK_BENCHMARK
        tradeHistory.push_back({buyId, sellId, price, quantity});
#else
        (void)buyId; (void)sellId; (void)price; (void)quantity;
#endif
    }

    inline void bumpProcessed() {
#ifndef ORDERBOOK_BENCHMARK
        ++processedCount;
#endif
    }

public:

    // -----------------------------------------------------------------------
    // LIMIT BUY  — verbatim from list<>.cpp / deque<>.cpp
    // -----------------------------------------------------------------------
    int buy(int price, int qty) {

        int buyId = nextId++;

        while (qty > 0 &&
               !asks.empty() &&
               asks.begin()->first <= price) {

            auto askIt = asks.begin();

            Order &sellOrder = askIt->second.front();

            int traded = std::min(qty, sellOrder.quantity);

            recordTrade(buyId, sellOrder.id, askIt->first, traded);

            qty -= traded;
            sellOrder.quantity -= traded;

            if (sellOrder.quantity == 0) {

                int filledId = sellOrder.id;

                askIt->second.pop_front();

                orderMap.erase(filledId);

                if (askIt->second.empty())
                    asks.erase(askIt);
            }
        }

        bumpProcessed();

        if (qty > 0) {

            bids[price].push_back({buyId, price, qty});

            auto it = std::prev(bids[price].end());

            orderMap[buyId] = {true, price, it};

            return buyId;
        }

        return -1;
    }

    // -----------------------------------------------------------------------
    // LIMIT SELL — verbatim from list<>.cpp / deque<>.cpp
    // -----------------------------------------------------------------------
    int sell(int price, int qty) {

        int sellId = nextId++;

        while (qty > 0 &&
               !bids.empty() &&
               bids.begin()->first >= price) {

            auto bidIt = bids.begin();

            Order &buyOrder = bidIt->second.front();

            int traded = std::min(qty, buyOrder.quantity);

            recordTrade(buyOrder.id, sellId, bidIt->first, traded);

            qty -= traded;
            buyOrder.quantity -= traded;

            if (buyOrder.quantity == 0) {

                int filledId = buyOrder.id;

                bidIt->second.pop_front();

                orderMap.erase(filledId);

                if (bidIt->second.empty())
                    bids.erase(bidIt);
            }
        }

        bumpProcessed();

        if (qty > 0) {

            asks[price].push_back({sellId, price, qty});

            auto it = std::prev(asks[price].end());

            orderMap[sellId] = {false, price, it};

            return sellId;
        }

        return -1;
    }

    // -----------------------------------------------------------------------
    // MARKET BUY — verbatim.  Consumes best asks; never rests.
    // -----------------------------------------------------------------------
    void marketBuy(int qty) {

        while (qty > 0 && !asks.empty()) {

            auto askIt = asks.begin();

            Order &sellOrder = askIt->second.front();

            int traded = std::min(qty, sellOrder.quantity);

            recordTrade(-1, sellOrder.id, askIt->first, traded);

            qty -= traded;
            sellOrder.quantity -= traded;

            if (sellOrder.quantity == 0) {

                int id = sellOrder.id;

                askIt->second.pop_front();

                orderMap.erase(id);

                if (askIt->second.empty())
                    asks.erase(askIt);
            }
        }

        bumpProcessed();
    }

    // -----------------------------------------------------------------------
    // MARKET SELL — verbatim.  Consumes best bids; never rests.
    // -----------------------------------------------------------------------
    void marketSell(int qty) {

        while (qty > 0 && !bids.empty()) {

            auto bidIt = bids.begin();

            Order &buyOrder = bidIt->second.front();

            int traded = std::min(qty, buyOrder.quantity);

            recordTrade(buyOrder.id, -1, bidIt->first, traded);

            qty -= traded;
            buyOrder.quantity -= traded;

            if (buyOrder.quantity == 0) {

                int id = buyOrder.id;

                bidIt->second.pop_front();

                orderMap.erase(id);

                if (bidIt->second.empty())
                    bids.erase(bidIt);
            }
        }

        bumpProcessed();
    }

    // -----------------------------------------------------------------------
    // CANCEL — verbatim.  O(1) hash lookup + O(1) list erase via the stored
    // iterator, then O(log N) map erase only if the level became empty.
    // -----------------------------------------------------------------------
    bool cancelOrder(int id) {

        auto found = orderMap.find(id);

        if (found == orderMap.end())
            return false;

        OrderInfo info = found->second;

        if (info.isBuy) {

            auto level = bids.find(info.price);

            level->second.erase(info.it);

            if (level->second.empty())
                bids.erase(level);
        }
        else {

            auto level = asks.find(info.price);

            level->second.erase(info.it);

            if (level->second.empty())
                asks.erase(level);
        }

        orderMap.erase(id);

        return true;
    }

    // -----------------------------------------------------------------------
    // MARKET INFO — verbatim.
    // -----------------------------------------------------------------------
    int getBestBid() {
        if (bids.empty())
            return -1;
        return bids.begin()->first;
    }

    int getBestAsk() {
        if (asks.empty())
            return -1;
        return asks.begin()->first;
    }

    int getSpread() {
        if (bids.empty() || asks.empty())
            return -1;
        return getBestAsk() - getBestBid();
    }

    // =======================================================================
    // ADDITIONS BELOW THIS LINE.  None of these mutate the book.
    // =======================================================================

    // The id buy()/sell() will hand to the NEXT limit order.  Needed because
    // buy()/sell() return -1 when the order fully filled without resting.
    int peekNextOrderId() const {
        return nextId;
    }

    // Draw an id from the same monotonic counter.  Used for MARKET orders,
    // which the original engine never assigned an id to.
    int reserveOrderId() {
        return nextId++;
    }

    // Number of resting (still cancellable) orders.  orderMap holds exactly
    // the live resting orders, so its size is the answer.
    std::size_t activeOrderCount() const {
        return orderMap.size();
    }

    long long ordersProcessed() const {
        return processedCount;
    }

    bool isResting(int id) const {
        return orderMap.find(id) != orderMap.end();
    }

    // Resting quantity of a single order, or -1 if it is not on the book.
    int restingQuantity(int id) const {
        auto found = orderMap.find(id);
        if (found == orderMap.end())
            return -1;
        return found->second.it->quantity;
    }

    // True if the resting order is on the buy side.  Only valid when
    // isResting(id) is true.
    bool isRestingBuy(int id) const {
        auto found = orderMap.find(id);
        if (found == orderMap.end())
            return false;
        return found->second.isBuy;
    }

    int restingPrice(int id) const {
        auto found = orderMap.find(id);
        if (found == orderMap.end())
            return -1;
        return found->second.price;
    }

    // Aggregated bid levels, best (highest) first — the map is already in
    // that order, so this is a straight walk.
    std::vector<BookLevel> snapshotBids(std::size_t depth = 0) const {
        std::vector<BookLevel> out;
        for (const auto &level : bids) {
            if (depth && out.size() >= depth) break;
            int total = 0;
            for (const auto &o : level.second)
                total += o.quantity;
            out.push_back({level.first, total});
        }
        return out;
    }

    // Aggregated ask levels, best (lowest) first.
    std::vector<BookLevel> snapshotAsks(std::size_t depth = 0) const {
        std::vector<BookLevel> out;
        for (const auto &level : asks) {
            if (depth && out.size() >= depth) break;
            int total = 0;
            for (const auto &o : level.second)
                total += o.quantity;
            out.push_back({level.first, total});
        }
        return out;
    }

    const std::vector<Trade> &trades() const {
        return tradeHistory;
    }

    std::size_t tradeCount() const {
        return tradeHistory.size();
    }

    // -----------------------------------------------------------------------
    // PRINTERS — verbatim from list<>.cpp.
    // -----------------------------------------------------------------------
    void printTrades() {

        std::cout << "\nTRADE HISTORY\n";

        for (auto &t : tradeHistory) {
            std::cout << "BUY " << t.buyId
                      << " <-> SELL " << t.sellId
                      << " : " << t.quantity
                      << " @ " << t.price
                      << "\n";
        }
    }

    void printBook() {

        std::cout << "\n----- ASKS -----\n";

        for (auto &level : asks) {
            int totalQty = 0;
            for (auto &o : level.second)
                totalQty += o.quantity;
            std::cout << level.first << " -> " << totalQty << "\n";
        }

        std::cout << "----------------\n";

        for (auto &level : bids) {
            int totalQty = 0;
            for (auto &o : level.second)
                totalQty += o.quantity;
            std::cout << level.first << " -> " << totalQty << "\n";
        }

        std::cout << "----- BIDS -----\n";
    }
};

#endif // ORDERBOOK_HPP
