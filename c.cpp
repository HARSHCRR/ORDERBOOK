#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

using namespace std;

struct Order {
    string type;
    int price;
    int quantity;
};

class OrderBook {
private:
    vector<Order> buys;
    vector<Order> sells;

public:

    void addOrder(string type, int price, int qty) {

        Order newOrder = {type, price, qty};

        if(type == "BUY") {

            for(auto &sell : sells) {

                if(sell.quantity > 0 &&
                   newOrder.quantity > 0 &&
                   newOrder.price >= sell.price) {

                    int traded =
                        min(newOrder.quantity, sell.quantity);

                    cout << "TRADE -> "
                         << traded
                         << " shares @ "
                         << sell.price
                         << endl;

                    newOrder.quantity -= traded;
                    sell.quantity -= traded;
                }
            }

            if(newOrder.quantity > 0)
                buys.push_back(newOrder);
        }

        else {

            for(auto &buy : buys) {

                if(buy.quantity > 0 &&
                   newOrder.quantity > 0 &&
                   buy.price >= newOrder.price) {

                    int traded =
                        min(newOrder.quantity, buy.quantity);

                    cout << "TRADE -> "
                         << traded
                         << " shares @ "
                         << buy.price
                         << endl;

                    newOrder.quantity -= traded;
                    buy.quantity -= traded;
                }
            }

            if(newOrder.quantity > 0)
                sells.push_back(newOrder);
        }
    }

    void printBook() {

        cout << "\nSELL ORDERS\n";

        for(auto &s : sells) {
            if(s.quantity > 0)
                cout << s.price
                     << " x "
                     << s.quantity
                     << endl;
        }

        cout << "\nBUY ORDERS\n";

        for(auto &b : buys) {
            if(b.quantity > 0)
                cout << b.price
                     << " x "
                     << b.quantity
                     << endl;
        }
    }
};

int main() {

    OrderBook ob;

    ob.addOrder("BUY",100,10);

    ob.addOrder("SELL",105,5);

    ob.addOrder("SELL",99,4);

    ob.addOrder("BUY",110,3);

    ob.printBook();
}