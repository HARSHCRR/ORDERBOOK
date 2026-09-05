// ---------------------------------------------------------------------------
// benchmark.cpp
//
// The benchmark from the ORIGINAL "list<>.cpp" main(), re-pointed at the
// extracted engine/OrderBook.hpp.  Same RNG seed (42), same price range
// (95-105), same quantity range (1-100), same 4-way uniform order mix, same
// 10,000,000 orders.
//
// Its ONLY purpose is to prove the header extraction did not cost anything.
// The canonical, untouched benchmark is still "list<>.cpp" at the repo root
// and that is the one the README's numbers come from.
//
//     g++ -O2 -std=c++17 -DORDERBOOK_BENCHMARK -o bench engine/benchmark.cpp
//
// -DORDERBOOK_BENCHMARK compiles out trade recording and the order counter,
// which is the exact configuration list<>.cpp was measured in (its
// tradeHistory.push_back calls were commented out).
// ---------------------------------------------------------------------------

#include <iostream>
#include <chrono>
#include <random>

#include "OrderBook.hpp"

using namespace std;

int main() {

    OrderBook ob;

    mt19937 rng(42);

    uniform_int_distribution<int> priceDist(95, 105);
    uniform_int_distribution<int> qtyDist(1, 100);
    uniform_int_distribution<int> typeDist(0, 3);

    const int NUM_ORDERS = 10000000;

    auto start = chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_ORDERS; i++) {

        int type  = typeDist(rng);
        int price = priceDist(rng);
        int qty   = qtyDist(rng);

        switch (type) {
            case 0: ob.buy(price, qty);   break;
            case 1: ob.sell(price, qty);  break;
            case 2: ob.marketBuy(qty);    break;
            case 3: ob.marketSell(qty);   break;
        }
    }

    auto end = chrono::high_resolution_clock::now();

    chrono::duration<double> elapsed = end - start;

    cout << "Processed " << NUM_ORDERS
         << " orders in " << elapsed.count() << " seconds\n";

    cout << "Throughput = "
         << (double)NUM_ORDERS / elapsed.count() << " orders/sec\n";

    double ns_per_order = elapsed.count() * 1e9 / NUM_ORDERS;

    cout << ns_per_order << " ns/order\n";

#ifdef ORDERBOOK_BENCHMARK
    cout << "(trade recording: OFF  -- matches list<>.cpp benchmark config)\n";
#else
    cout << "(trade recording: ON   -- NOT the list<>.cpp benchmark config)\n";
#endif

    return 0;
}
