#include <iostream>
#include <map>
#include <list>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <random>
#include <mutex>
#include <thread>

using namespace std;

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
    list<Order>::iterator it;
};

class OrderBook {

private:

    map<int, list<Order>, greater<int>> bids;
    map<int, list<Order>> asks;

    unordered_map<int, OrderInfo> orderMap;

    vector<Trade> tradeHistory;

    int nextId = 1;

    // Single coarse-grained mutex protecting all shared state
    // (bids, asks, orderMap, tradeHistory, nextId).
    mutable mutex mtx;

public:

    int buy(int price, int qty) {

        lock_guard<mutex> lock(mtx);

        int buyId = nextId++;

        while (qty > 0 &&
               !asks.empty() &&
               asks.begin()->first <= price) {

            auto askIt = asks.begin();

            Order &sellOrder = askIt->second.front();

            int traded = min(qty, sellOrder.quantity);

          

            //tradeHistory.push_back(
             //   {buyId, sellOrder.id, askIt->first, traded}
            //);

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

        if (qty > 0) {

            bids[price].push_back({buyId, price, qty});

            auto it = prev(bids[price].end());

            orderMap[buyId] = {true, price, it};

            return buyId;
        }

        return -1;
    }

    int sell(int price, int qty) {

        lock_guard<mutex> lock(mtx);

        int sellId = nextId++;

        while (qty > 0 &&
               !bids.empty() &&
               bids.begin()->first >= price) {

            auto bidIt = bids.begin();

            Order &buyOrder = bidIt->second.front();

            int traded = min(qty, buyOrder.quantity);

        

            //tradeHistory.push_back(
            //    {buyOrder.id, sellId, bidIt->first, traded}
            //);

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

        if (qty > 0) {

            asks[price].push_back({sellId, price, qty});

            auto it = prev(asks[price].end());

            orderMap[sellId] = {false, price, it};

            return sellId;
        }

        return -1;
    }

    void marketBuy(int qty) {

        lock_guard<mutex> lock(mtx);

        while (qty > 0 && !asks.empty()) {

            auto askIt = asks.begin();

            Order &sellOrder = askIt->second.front();

            int traded = min(qty, sellOrder.quantity);

        

            //tradeHistory.push_back(
            //    {-1, sellOrder.id, askIt->first, traded}
            //);

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
    }

    void marketSell(int qty) {

        lock_guard<mutex> lock(mtx);

        while (qty > 0 && !bids.empty()) {

            auto bidIt = bids.begin();

            Order &buyOrder = bidIt->second.front();

            int traded = min(qty, buyOrder.quantity);


            //tradeHistory.push_back(
            //    {buyOrder.id, -1, bidIt->first, traded}
            //);

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
    }

    bool cancelOrder(int id) {

        lock_guard<mutex> lock(mtx);

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

    int getBestBid() {

        lock_guard<mutex> lock(mtx);

        if (bids.empty())
            return -1;

        return bids.begin()->first;
    }

    int getBestAsk() {

        lock_guard<mutex> lock(mtx);

        if (asks.empty())
            return -1;

        return asks.begin()->first;
    }

    int getSpread() {

        // NOTE: does not lock itself, and calls getBestBid()/getBestAsk(),
        // which do lock. This is fine (no double-lock, no deadlock) because
        // mutex is released after each of those calls returns. There is a
        // small window between the two calls where the book could change
        // in another thread, so the "spread" is a best-effort snapshot,
        // not an atomic read of both sides at once. Good enough for
        // "basic" thread safety; see explanation below for how to fix it.

        int bid = getBestBid();
        int ask = getBestAsk();

        if (bid == -1 || ask == -1)
            return -1;

        return ask - bid;
    }

};

int main() {

    OrderBook book;

    // Seed the book with some resting liquidity first (single-threaded,
    // so no race here).
    book.sell(101, 50);
    book.sell(102, 30);
    book.buy(99, 40);
    book.buy(98, 20);

    const int NUM_THREADS = 4;
    const int ORDERS_PER_THREAD = 25;

    vector<thread> workers;

    // Launch several threads that all hit the SAME OrderBook instance
    // concurrently, mixing buys and sells. This is the actual
    // multithreading: multiple threads calling public methods on shared
    // state at the same time. The mutex inside OrderBook is what makes
    // this safe instead of undefined behavior.
    for (int t = 0; t < NUM_THREADS; ++t) {

        workers.emplace_back([&book, t]() {

            mt19937 rng(1000 + t);
            uniform_int_distribution<int> priceDist(95, 105);
            uniform_int_distribution<int> qtyDist(1, 10);
            uniform_int_distribution<int> sideDist(0, 1);

            for (int i = 0; i < ORDERS_PER_THREAD; ++i) {

                int price = priceDist(rng);
                int qty = qtyDist(rng);

                if (sideDist(rng) == 0)
                    book.buy(price, qty);
                else
                    book.sell(price, qty);
            }
        });
    }

    // Wait for all worker threads to finish before reading final state.
    for (auto &w : workers)
        w.join();

    cout << "Best bid: " << book.getBestBid() << "\n";
    cout << "Best ask: " << book.getBestAsk() << "\n";
    cout << "Spread: "   << book.getSpread()   << "\n";

    return 0;
}