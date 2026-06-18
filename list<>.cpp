#include <iostream>
#include <map>
#include <list>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <random>

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

public:

    int buy(int price, int qty) {

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

    void printTrades() {

        cout << "\nTRADE HISTORY\n";

        for (auto &t : tradeHistory) {

            cout << "BUY "
                 << t.buyId
                 << " <-> SELL "
                 << t.sellId
                 << " : "
                 << t.quantity
                 << " @ "
                 << t.price
                 << endl;
        }
    }

    void printBook() {

        cout << "\n----- ASKS -----\n";

        for (auto &level : asks) {

            int totalQty = 0;

            for (auto &o : level.second)
                totalQty += o.quantity;

            cout << level.first
                 << " -> "
                 << totalQty
                 << endl;
        }

        cout << "----------------\n";

        for (auto &level : bids) {

            int totalQty = 0;

            for (auto &o : level.second)
                totalQty += o.quantity;

            cout << level.first
                 << " -> "
                 << totalQty
                 << endl;
        }

        cout << "----- BIDS -----\n";
    }
};

int main() {

    OrderBook ob;

    mt19937 rng(42);

    uniform_int_distribution<int> priceDist(95, 105);
    uniform_int_distribution<int> qtyDist(1, 100);
    uniform_int_distribution<int> typeDist(0, 3);

    const int NUM_ORDERS = 10000000;

    auto start = chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_ORDERS; i++) {

        int type = typeDist(rng);
        int price = priceDist(rng);
        int qty = qtyDist(rng);

        switch (type) {

            case 0:
                ob.buy(price, qty);
                break;

            case 1:
                ob.sell(price, qty);
                break;

            case 2:
                ob.marketBuy(qty);
                break;

            case 3:
                ob.marketSell(qty);
                break;
        }
    }

    auto end = chrono::high_resolution_clock::now();

    chrono::duration<double> elapsed = end - start;

    cout << "Processed "
         << NUM_ORDERS
         << " orders in "
         << elapsed.count()
         << " seconds\n";

    cout << "Throughput = "
         << (double)NUM_ORDERS / elapsed.count()
         << " orders/sec\n";

         double ns_per_order =
    elapsed.count() * 1e9 / NUM_ORDERS;

cout << ns_per_order << " ns/order\n";

    return 0;
}