// ---------------------------------------------------------------------------
// engine_tests.cpp
//
// Matching-semantics tests for the extracted engine.  These pin down the
// behaviour that already existed in list<>.cpp so that any future change to
// the header is caught immediately.  No framework: a CHECK macro and a
// non-zero exit code are enough.
//
//     g++ -O2 -std=c++17 -o engine_tests engine/engine_tests.cpp && ./engine_tests
// ---------------------------------------------------------------------------

#include <iostream>
#include <string>

#include "OrderBook.hpp"

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        ++checks;                                                             \
        if (!(cond)) {                                                        \
            ++failures;                                                       \
            std::cout << "  FAIL: " << (msg)                                  \
                      << "   [" << #cond << " @ line " << __LINE__ << "]\n";  \
        }                                                                     \
    } while (0)

#define CHECK_EQ(actual, expected, msg)                                       \
    do {                                                                      \
        ++checks;                                                             \
        auto a_ = (actual);                                                   \
        auto e_ = (expected);                                                 \
        if (!(a_ == e_)) {                                                    \
            ++failures;                                                       \
            std::cout << "  FAIL: " << (msg) << "   got " << a_               \
                      << ", expected " << e_                                  \
                      << " @ line " << __LINE__ << "\n";                      \
        }                                                                     \
    } while (0)

static void banner(const std::string &name) {
    std::cout << "[ " << name << " ]\n";
}

// ---------------------------------------------------------------------------
int main() {

    // -----------------------------------------------------------------------
    banner("resting orders sit on the book");
    {
        OrderBook ob;
        int bid = ob.buy(100, 10);
        int ask = ob.sell(101, 5);

        CHECK(bid > 0, "non-crossing limit buy returns its id");
        CHECK(ask > 0, "non-crossing limit sell returns its id");
        CHECK_EQ(ob.getBestBid(), 100, "best bid");
        CHECK_EQ(ob.getBestAsk(), 101, "best ask");
        CHECK_EQ(ob.getSpread(), 1, "spread");
        CHECK_EQ(ob.activeOrderCount(), (std::size_t)2, "both orders resting");
        CHECK_EQ(ob.tradeCount(), (std::size_t)0, "no trades yet");
    }

    // -----------------------------------------------------------------------
    banner("crossing sell trades against the resting bid");
    {
        OrderBook ob;
        ob.buy(100, 10);
        std::size_t before = ob.tradeCount();
        ob.sell(100, 10);

        CHECK_EQ(ob.tradeCount() - before, (std::size_t)1, "one trade");
        CHECK_EQ(ob.trades().back().price, 100, "trade at the resting price");
        CHECK_EQ(ob.trades().back().quantity, 10, "full quantity");
        CHECK_EQ(ob.activeOrderCount(), (std::size_t)0, "book empty");
        CHECK_EQ(ob.getBestBid(), -1, "no bid left");
    }

    // -----------------------------------------------------------------------
    banner("partial fill: incoming larger than resting");
    {
        OrderBook ob;
        ob.buy(100, 10);
        int rest = ob.peekNextOrderId();
        int ret  = ob.sell(100, 25);

        CHECK_EQ(ret, rest, "sell rests with the peeked id");
        CHECK_EQ(ob.trades().size(), (std::size_t)1, "one fill of 10");
        CHECK_EQ(ob.trades()[0].quantity, 10, "filled only what was there");
        CHECK_EQ(ob.restingQuantity(rest), 15, "remainder rests");
        CHECK_EQ(ob.getBestAsk(), 100, "remainder is now the best ask");
    }

    // -----------------------------------------------------------------------
    banner("partial fill: resting larger than incoming");
    {
        OrderBook ob;
        int rest = ob.buy(100, 30);
        ob.sell(100, 12);

        CHECK_EQ(ob.trades().size(), (std::size_t)1, "one fill");
        CHECK_EQ(ob.trades()[0].quantity, 12, "filled the incoming size");
        CHECK_EQ(ob.restingQuantity(rest), 18, "resting order decremented");
        CHECK_EQ(ob.activeOrderCount(), (std::size_t)1, "still one resting order");
    }

    // -----------------------------------------------------------------------
    banner("full fill returns -1 (original engine contract)");
    {
        OrderBook ob;
        ob.sell(100, 10);
        int ret = ob.buy(100, 10);
        CHECK_EQ(ret, -1, "fully filled order never rests, returns -1");
    }

    // -----------------------------------------------------------------------
    banner("FIFO: earliest order at a price fills first");
    {
        OrderBook ob;
        int a = ob.buy(100, 5);   // first
        int b = ob.buy(100, 5);   // second
        int c = ob.buy(99, 5);    // worse price

        ob.sell(100, 5);          // should hit A only

        CHECK_EQ(ob.trades().size(), (std::size_t)1, "one fill");
        CHECK_EQ(ob.trades()[0].buyId, a, "A filled first (time priority)");
        CHECK(!ob.isResting(a), "A fully consumed");
        CHECK(ob.isResting(b), "B still resting");
        CHECK(ob.isResting(c), "C untouched");
    }

    // -----------------------------------------------------------------------
    banner("price priority: best price fills before worse price");
    {
        OrderBook ob;
        int worse = ob.buy(99, 5);
        int best  = ob.buy(101, 5);

        ob.sell(99, 5);

        CHECK_EQ(ob.trades()[0].buyId, best, "highest bid filled first");
        CHECK_EQ(ob.trades()[0].price, 101, "traded at the resting bid price");
        CHECK(ob.isResting(worse), "worse price untouched");
    }

    // -----------------------------------------------------------------------
    banner("one order sweeping multiple resting orders");
    {
        OrderBook ob;
        ob.sell(101, 5);
        ob.sell(101, 5);
        ob.sell(102, 5);

        ob.buy(102, 15);

        CHECK_EQ(ob.trades().size(), (std::size_t)3, "three fills");
        CHECK_EQ(ob.trades()[0].price, 101, "cheapest ask first");
        CHECK_EQ(ob.trades()[1].price, 101, "then its FIFO neighbour");
        CHECK_EQ(ob.trades()[2].price, 102, "then the next level up");
        CHECK_EQ(ob.activeOrderCount(), (std::size_t)0, "ask side cleared");
    }

    // -----------------------------------------------------------------------
    banner("cancellation via the iterator map");
    {
        OrderBook ob;
        int a = ob.buy(100, 5);
        int b = ob.buy(100, 7);

        CHECK(ob.cancelOrder(a), "cancel succeeds");
        CHECK(!ob.isResting(a), "A gone");
        CHECK(ob.isResting(b), "B untouched");
        CHECK_EQ(ob.getBestBid(), 100, "price level survives while B is there");

        CHECK(!ob.cancelOrder(a), "double cancel is a no-op");
        CHECK(!ob.cancelOrder(999999), "unknown id is a no-op");

        CHECK(ob.cancelOrder(b), "cancel B");
        CHECK_EQ(ob.getBestBid(), -1, "empty level was erased from the map");
    }

    // -----------------------------------------------------------------------
    banner("cancelled order does not trade");
    {
        OrderBook ob;
        int a = ob.buy(100, 5);
        ob.cancelOrder(a);
        ob.sell(100, 5);

        CHECK_EQ(ob.trades().size(), (std::size_t)0, "nothing to trade against");
        CHECK_EQ(ob.getBestAsk(), 100, "the sell rested instead");
    }

    // -----------------------------------------------------------------------
    banner("market buy consumes best asks");
    {
        OrderBook ob;
        ob.sell(101, 5);
        ob.sell(102, 5);

        ob.marketBuy(7);

        CHECK_EQ(ob.trades().size(), (std::size_t)2, "two fills");
        CHECK_EQ(ob.trades()[0].price, 101, "best ask first");
        CHECK_EQ(ob.trades()[0].quantity, 5, "took all of level 101");
        CHECK_EQ(ob.trades()[1].price, 102, "then walked up");
        CHECK_EQ(ob.trades()[1].quantity, 2, "took the remainder");
        CHECK_EQ(ob.trades()[0].buyId, -1, "market side has no resting id");
        CHECK_EQ(ob.getBestAsk(), 102, "level 102 partially left");
    }

    // -----------------------------------------------------------------------
    banner("market sell consumes best bids");
    {
        OrderBook ob;
        ob.buy(100, 5);
        ob.buy(99, 5);

        ob.marketSell(8);

        CHECK_EQ(ob.trades().size(), (std::size_t)2, "two fills");
        CHECK_EQ(ob.trades()[0].price, 100, "best bid first");
        CHECK_EQ(ob.trades()[1].price, 99, "then walked down");
        CHECK_EQ(ob.trades()[1].sellId, -1, "market side has no resting id");
        CHECK_EQ(ob.getBestBid(), 99, "level 99 partially left");
    }

    // -----------------------------------------------------------------------
    banner("market order into an empty book does nothing");
    {
        OrderBook ob;
        ob.marketBuy(10);
        ob.marketSell(10);

        CHECK_EQ(ob.trades().size(), (std::size_t)0, "no trades");
        CHECK_EQ(ob.activeOrderCount(), (std::size_t)0, "nothing rested");
    }

    // -----------------------------------------------------------------------
    banner("market order never rests the remainder");
    {
        OrderBook ob;
        ob.sell(101, 3);
        ob.marketBuy(10);

        CHECK_EQ(ob.trades().size(), (std::size_t)1, "filled what it could");
        CHECK_EQ(ob.trades()[0].quantity, 3, "3 of 10");
        CHECK_EQ(ob.activeOrderCount(), (std::size_t)0, "remainder discarded");
    }

    // -----------------------------------------------------------------------
    banner("order ids are unique and monotonic across limit + market");
    {
        OrderBook ob;
        int a = ob.buy(100, 1);
        int m = ob.reserveOrderId();
        ob.marketSell(1);
        int b = ob.sell(105, 1);

        CHECK(a < m, "market id follows the limit id");
        CHECK(m < b, "next limit id follows the market id");
    }

    // -----------------------------------------------------------------------
    banner("book snapshot aggregates quantity per price level");
    {
        OrderBook ob;
        ob.buy(100, 5);
        ob.buy(100, 7);
        ob.buy(99, 3);
        ob.sell(101, 4);

        auto bids = ob.snapshotBids();
        auto asks = ob.snapshotAsks();

        CHECK_EQ(bids.size(), (std::size_t)2, "two bid levels");
        CHECK_EQ(bids[0].price, 100, "best bid first");
        CHECK_EQ(bids[0].quantity, 12, "5 + 7 aggregated");
        CHECK_EQ(bids[1].price, 99, "then 99");
        CHECK_EQ(asks[0].price, 101, "best ask first");
        CHECK_EQ(asks[0].quantity, 4, "ask quantity");

        auto shallow = ob.snapshotBids(1);
        CHECK_EQ(shallow.size(), (std::size_t)1, "depth limit honoured");
    }

    // -----------------------------------------------------------------------
    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";

    if (failures) {
        std::cout << failures << " FAILURE(S)\n";
        return 1;
    }

    std::cout << "ALL ENGINE TESTS PASSED\n";
    return 0;
}
