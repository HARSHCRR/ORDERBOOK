// ---------------------------------------------------------------------------
// engine_server.cpp
//
// A persistent process that wraps the EXISTING OrderBook (engine/OrderBook.hpp)
// in a newline-delimited JSON protocol.
//
//     stdin :  one JSON object per line   (a command)
//     stdout:  one JSON object per line   (the response to that command)
//     stderr:  human-readable diagnostics only
//
// The loop is strictly request -> response, one at a time, in order.  There is
// no concurrency here at all: the engine is single-threaded by design and this
// process keeps it that way.  The FastAPI layer is what serialises callers.
//
// Debug it by hand:
//     ./engine_server
//     {"action":"PLACE_ORDER","side":"BUY","type":"LIMIT","price":100,"quantity":10}
//     {"action":"GET_ORDER_BOOK"}
//
// Commands:
//     PLACE_ORDER    side=BUY|SELL  type=LIMIT|MARKET  price(LIMIT only)  quantity
//     CANCEL_ORDER   order_id
//     GET_ORDER_BOOK [depth]
//     GET_TRADES     [limit]
//     GET_STATS
//     GET_ORDER      order_id
//     PING
//     SHUTDOWN
// ---------------------------------------------------------------------------

#include <iostream>
#include <string>
#include <cstddef>

#include "OrderBook.hpp"
#include "third_party/nlohmann/json.hpp"

using json = nlohmann::json;

namespace {

const std::size_t DEFAULT_BOOK_DEPTH  = 15;
const std::size_t DEFAULT_TRADE_LIMIT = 50;

OrderBook book;

// -1 is the engine's "no such price" sentinel.  Turn it into JSON null so the
// API layer never has to know about the sentinel.
json priceOrNull(int v) {
    if (v < 0) return json(nullptr);
    return json(v);
}

json bookSnapshot(std::size_t depth) {

    json bids = json::array();
    for (const auto &lvl : book.snapshotBids(depth))
        bids.push_back({{"price", lvl.price}, {"quantity", lvl.quantity}});

    json asks = json::array();
    for (const auto &lvl : book.snapshotAsks(depth))
        asks.push_back({{"price", lvl.price}, {"quantity", lvl.quantity}});

    return json{
        {"bids",     bids},
        {"asks",     asks},
        {"best_bid", priceOrNull(book.getBestBid())},
        {"best_ask", priceOrNull(book.getBestAsk())},
        {"spread",   priceOrNull(book.getSpread())}
    };
}

json statsSnapshot() {
    return json{
        {"orders_processed", book.ordersProcessed()},
        {"trades_executed",  static_cast<long long>(book.tradeCount())},
        {"active_orders",    static_cast<long long>(book.activeOrderCount())},
        {"best_bid",         priceOrNull(book.getBestBid())},
        {"best_ask",         priceOrNull(book.getBestAsk())},
        {"spread",           priceOrNull(book.getSpread())}
    };
}

// Serialise the trades appended by the command we just ran.  The engine
// appends to tradeHistory in execution order, so [before, end) is exactly the
// set of fills this one order produced.
json tradesSince(std::size_t before) {
    const std::vector<Trade> &all = book.trades();
    json out = json::array();
    for (std::size_t i = before; i < all.size(); ++i) {
        out.push_back({
            {"seq",      static_cast<long long>(i)},
            {"buy_id",   all[i].buyId},
            {"sell_id",  all[i].sellId},
            {"price",    all[i].price},
            {"quantity", all[i].quantity}
        });
    }
    return out;
}

json errorResponse(const std::string &code, const std::string &message) {
    return json{{"ok", false}, {"error", code}, {"message", message}};
}

// -------------------------------------------------------------------------
// PLACE_ORDER
//
// The engine's own entry points are called unchanged.  Everything reported
// back is DERIVED from the engine's state, never recomputed independently:
//
//   order_id           - for LIMIT, the id the engine is about to assign
//                        (peekNextOrderId), because buy()/sell() return -1
//                        when the order fully filled without resting.
//                        For MARKET, an id drawn from the same counter.
//   filled_quantity    - sum of the trades the engine appended.
//   remaining_quantity - requested - filled.
//   status             - FILLED / PARTIALLY_FILLED / RESTING / REJECTED.
// -------------------------------------------------------------------------
json handlePlaceOrder(const json &req) {

    std::string side = req.value("side", "");
    std::string type = req.value("type", "LIMIT");

    if (side != "BUY" && side != "SELL")
        return errorResponse("BAD_SIDE", "side must be BUY or SELL");

    if (type != "LIMIT" && type != "MARKET")
        return errorResponse("BAD_TYPE", "type must be LIMIT or MARKET");

    if (!req.contains("quantity") || !req["quantity"].is_number_integer())
        return errorResponse("BAD_QUANTITY", "quantity must be an integer");

    int quantity = req["quantity"].get<int>();
    if (quantity <= 0)
        return errorResponse("BAD_QUANTITY", "quantity must be > 0");

    int price = 0;
    if (type == "LIMIT") {
        if (!req.contains("price") || !req["price"].is_number_integer())
            return errorResponse("BAD_PRICE", "LIMIT orders require an integer price");
        price = req["price"].get<int>();
        if (price <= 0)
            return errorResponse("BAD_PRICE", "price must be > 0");
    }

    const std::size_t tradesBefore = book.tradeCount();

    int orderId;

    if (type == "LIMIT") {
        // The id buy()/sell() is about to consume.
        orderId = book.peekNextOrderId();
        if (side == "BUY") book.buy(price, quantity);
        else               book.sell(price, quantity);
    } else {
        // marketBuy/marketSell never allocated an id, so take one from the
        // same counter to keep ids unique and monotonic across all orders.
        orderId = book.reserveOrderId();
        if (side == "BUY") book.marketBuy(quantity);
        else               book.marketSell(quantity);
    }

    json fills = tradesSince(tradesBefore);

    int filled = 0;
    for (const auto &t : fills)
        filled += t["quantity"].get<int>();

    int remaining = quantity - filled;

    std::string status;
    if (filled == quantity)          status = "FILLED";
    else if (book.isResting(orderId)) status = (filled > 0) ? "PARTIALLY_FILLED" : "RESTING";
    else if (filled > 0)              status = "PARTIALLY_FILLED";  // MARKET, book ran dry
    else                              status = "REJECTED";          // MARKET into an empty book

    return json{
        {"ok",                 true},
        {"order_id",           orderId},
        {"side",               side},
        {"type",               type},
        {"price",              type == "LIMIT" ? json(price) : json(nullptr)},
        {"requested_quantity", quantity},
        {"filled_quantity",    filled},
        {"remaining_quantity", remaining},
        {"resting",            book.isResting(orderId)},
        {"status",             status},
        {"trades",             fills}
    };
}

json handleCancelOrder(const json &req) {

    if (!req.contains("order_id") || !req["order_id"].is_number_integer())
        return errorResponse("BAD_ORDER_ID", "order_id must be an integer");

    int id = req["order_id"].get<int>();

    // Straight through to the original iterator-based cancellation.
    bool cancelled = book.cancelOrder(id);

    return json{
        {"ok",        true},
        {"order_id",  id},
        {"cancelled", cancelled},
        {"status",    cancelled ? "CANCELLED" : "NOT_FOUND"}
    };
}

json handleGetOrder(const json &req) {

    if (!req.contains("order_id") || !req["order_id"].is_number_integer())
        return errorResponse("BAD_ORDER_ID", "order_id must be an integer");

    int id = req["order_id"].get<int>();

    // orderMap only holds RESTING orders.  Once an order is fully filled or
    // cancelled the engine drops it, so "not resting" is all we can say — the
    // engine deliberately keeps no per-order archive.
    if (!book.isResting(id)) {
        return json{
            {"ok",       true},
            {"order_id", id},
            {"resting",  false},
            {"status",   "NOT_RESTING"}
        };
    }

    return json{
        {"ok",                 true},
        {"order_id",           id},
        {"resting",            true},
        {"side",               book.isRestingBuy(id) ? "BUY" : "SELL"},
        {"price",              book.restingPrice(id)},
        {"remaining_quantity", book.restingQuantity(id)},
        {"status",             "RESTING"}
    };
}

json handleGetTrades(const json &req) {

    std::size_t limit = DEFAULT_TRADE_LIMIT;
    if (req.contains("limit") && req["limit"].is_number_integer()) {
        long long l = req["limit"].get<long long>();
        if (l > 0) limit = static_cast<std::size_t>(l);
    }

    const std::vector<Trade> &all = book.trades();
    std::size_t start = all.size() > limit ? all.size() - limit : 0;

    json out = tradesSince(start);

    return json{
        {"ok",     true},
        {"trades", out},
        {"total",  static_cast<long long>(all.size())}
    };
}

json dispatch(const json &req) {

    std::string action = req.value("action", "");

    if (action == "PLACE_ORDER")    return handlePlaceOrder(req);
    if (action == "CANCEL_ORDER")   return handleCancelOrder(req);
    if (action == "GET_ORDER")      return handleGetOrder(req);
    if (action == "GET_TRADES")     return handleGetTrades(req);

    if (action == "GET_ORDER_BOOK") {
        std::size_t depth = DEFAULT_BOOK_DEPTH;
        if (req.contains("depth") && req["depth"].is_number_integer()) {
            long long d = req["depth"].get<long long>();
            depth = (d <= 0) ? 0 : static_cast<std::size_t>(d);  // 0 = full book
        }
        json r = bookSnapshot(depth);
        r["ok"] = true;
        return r;
    }

    if (action == "GET_STATS") {
        json r = statsSnapshot();
        r["ok"] = true;
        return r;
    }

    if (action == "PING")
        return json{{"ok", true}, {"pong", true}};

    return errorResponse("UNKNOWN_ACTION", "unknown action: " + action);
}

} // namespace

int main() {

    // Unbuffered-ish: flush after every response so the parent process never
    // blocks waiting on a buffer that is not full.
    std::ios::sync_with_stdio(false);

    std::cerr << "engine_server ready (NDJSON on stdin/stdout)\n";

    std::string line;

    while (std::getline(std::cin, line)) {

        if (line.empty())
            continue;

        json response;
        json request;

        // A malformed line must not kill the engine — report and carry on, so
        // the book state survives a bad client.
        try {
            request = json::parse(line);
        } catch (const std::exception &e) {
            response = errorResponse("BAD_JSON", e.what());
            std::cout << response.dump() << std::endl;
            continue;
        }

        // Echo the caller's correlation id back, if they sent one.
        json requestId = request.contains("id") ? request["id"] : json(nullptr);

        if (request.value("action", "") == "SHUTDOWN") {
            std::cout << json{{"ok", true}, {"id", requestId},
                              {"shutdown", true}}.dump() << std::endl;
            break;
        }

        try {
            response = dispatch(request);
        } catch (const std::exception &e) {
            response = errorResponse("ENGINE_ERROR", e.what());
        }

        response["id"] = requestId;

        std::cout << response.dump() << std::endl;
    }

    return 0;
}
