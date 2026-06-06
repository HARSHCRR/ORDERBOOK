#include <iostream>
#include <map>
#include <queue>
#include <chrono>

using namespace std;
using namespace std::chrono;

struct Order {
    int id;
    int quantity;
};

class OrderBook {
private:
    map<int, queue<Order>, greater<int>> bids;
    map<int, queue<Order>> asks;

    int nextId = 1;

public:
    void addBuy(int price, int qty) {

        while(qty > 0 &&
              !asks.empty() &&
              asks.begin()->first <= price) {

            auto askIt = asks.begin();

            int askPrice = askIt->first;

            Order &sellOrder = askIt->second.front();

            int traded = min(qty, sellOrder.quantity);


            qty -= traded;
            sellOrder.quantity -= traded;

            if(sellOrder.quantity == 0) {
                askIt->second.pop();
            }

            if(askIt->second.empty()) {
                asks.erase(askIt);
            }
        }

        if(qty > 0) {
            bids[price].push({nextId++, qty});
        }
    }

    void addSell(int price, int qty) {

        while(qty > 0 &&
              !bids.empty() &&
              bids.begin()->first >= price) {

            auto bidIt = bids.begin();

            int bidPrice = bidIt->first;

            Order &buyOrder = bidIt->second.front();

            int traded = min(qty, buyOrder.quantity);

            

            qty -= traded;
            buyOrder.quantity -= traded;

            if(buyOrder.quantity == 0) {
                bidIt->second.pop();
            }

            if(bidIt->second.empty()) {
                bids.erase(bidIt);
            }
        }

        if(qty > 0) {
            asks[price].push({nextId++, qty});
        }
    }

    void printBook() {

        cout << "\nASKS\n";

        for(auto &level : asks) {

            cout << level.first << " : ";

            queue<Order> q = level.second;

            while(!q.empty()) {
                cout << q.front().quantity << " ";
                q.pop();
            }

            cout << endl;
        }

        cout << "\nBIDS\n";

        for(auto &level : bids) {

            cout << level.first << " : ";

            queue<Order> q = level.second;

            while(!q.empty()) {
                cout << q.front().quantity << " ";
                q.pop();
            }

            cout << endl;
        }
    }
};

int main() {

    auto start = high_resolution_clock::now();

    OrderBook ob;

    for(int i = 0; i < 1000000000; i++) {
    ob.addBuy(100 + (i % 5), 1 + (i % 10));
}

for(int i = 0; i < 1000000000; i++) {
    ob.addSell(102 + (i % 5), 1 + (i % 10));
}

    auto stop = high_resolution_clock::now();

    auto duration =
        duration_cast<microseconds>(stop - start);

    cout << "\nExecution time = "
         << duration.count()
         << " microseconds\n";

    

    return 0;
}
