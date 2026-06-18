#include <iostream>
#include <map>
#include <deque>
#include <vector>
#include <unordered_map>

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
    deque<Order>::iterator it;
};

class OrderBook {

private:

    map<int, deque<Order>, greater<int>> bids;
    map<int, deque<Order>> asks;

    unordered_map<int, OrderInfo> orderMap;

    vector<Trade> tradeHistory;

    int nextId = 1;

public:

    //----------------------------------------
    // LIMIT BUY
    //----------------------------------------
    int buy(int price, int qty) {

        int buyId = nextId++;

        while (qty > 0 &&
               !asks.empty() &&
               asks.begin()->first <= price) {

            auto askIt = asks.begin();

            Order &sellOrder = askIt->second.front();

            int traded = min(qty, sellOrder.quantity);

            tradeHistory.push_back(
                {buyId, sellOrder.id, askIt->first, traded}
            );

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

    //----------------------------------------
    // LIMIT SELL
    //----------------------------------------
    int sell(int price, int qty) {

        int sellId = nextId++;

        while (qty > 0 &&
               !bids.empty() &&
               bids.begin()->first >= price) {

            auto bidIt = bids.begin();

            Order &buyOrder = bidIt->second.front();

            int traded = min(qty, buyOrder.quantity);

            tradeHistory.push_back(
                {buyOrder.id, sellId, bidIt->first, traded}
            );

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

    //----------------------------------------
    // MARKET BUY
    //----------------------------------------
    void marketBuy(int qty) {

        while (qty > 0 && !asks.empty()) {

            auto askIt = asks.begin();

            Order &sellOrder = askIt->second.front();

            int traded = min(qty, sellOrder.quantity);

            tradeHistory.push_back(
                {-1, sellOrder.id, askIt->first, traded}
            );

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

    //----------------------------------------
    // MARKET SELL
    //----------------------------------------
    void marketSell(int qty) {

        while (qty > 0 && !bids.empty()) {

            auto bidIt = bids.begin();

            Order &buyOrder = bidIt->second.front();

            int traded = min(qty, buyOrder.quantity);

            tradeHistory.push_back(
                {buyOrder.id, -1, bidIt->first, traded}
            );

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

    //----------------------------------------
    // CANCEL ORDER
    //----------------------------------------
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

    //----------------------------------------
    // BEST BID / ASK
    //----------------------------------------
    int getBestBid() {
        return bids.empty() ? -1 : bids.begin()->first;
    }

    int getBestAsk() {
        return asks.empty() ? -1 : asks.begin()->first;
    }

    int getSpread() {

        if (bids.empty() || asks.empty())
            return -1;

        return getBestAsk() - getBestBid();
    }

    //----------------------------------------
    // PRINT TRADE HISTORY
    //----------------------------------------
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
                 << '\n';
        }
    }

    //----------------------------------------
    // PRINT BOOK
    //----------------------------------------
    void printBook() {

        cout << "\n----- ASKS -----\n";

        for (auto &level : asks) {

            int totalQty = 0;

            for (auto &o : level.second)
                totalQty += o.quantity;

            cout << level.first
                 << " -> "
                 << totalQty
                 << '\n';
        }

        cout << "----------------\n";

        for (auto &level : bids) {

            int totalQty = 0;

            for (auto &o : level.second)
                totalQty += o.quantity;

            cout << level.first
                 << " -> "
                 << totalQty
                 << '\n';
        }

        cout << "----- BIDS -----\n";
    }
};