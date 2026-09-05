"""
End-to-end API tests: HTTP -> FastAPI -> bridge -> C++ engine -> back.

These run against a REAL engine subprocess, not a mock. If the C++ matching
logic ever changes, these fail. That is the point: they are the contract
between the web layer and the engine.

Numbered to match the acceptance checklist in the README.
"""

from conftest import place


# --- 1-3: orders rest on the book ------------------------------------------
def test_01_limit_buy_rests(client):
    report = place(client, "BUY", 10, price=100)

    assert report["order_id"] == 1
    assert report["status"] == "RESTING"
    assert report["filled_quantity"] == 0
    assert report["remaining_quantity"] == 10
    assert report["resting"] is True
    assert report["trades"] == []


def test_02_limit_sell_above_bid_rests(client):
    place(client, "BUY", 10, price=100)
    report = place(client, "SELL", 5, price=101)

    assert report["status"] == "RESTING"
    assert report["filled_quantity"] == 0, "101 does not cross a bid of 100"


def test_03_both_sides_visible_on_the_book(client):
    place(client, "BUY", 10, price=100)
    place(client, "SELL", 5, price=101)

    book = client.get("/book").json()

    assert book["bids"] == [{"price": 100, "quantity": 10}]
    assert book["asks"] == [{"price": 101, "quantity": 5}]
    assert book["best_bid"] == 100
    assert book["best_ask"] == 101
    assert book["spread"] == 1


# --- 4-5: a crossing order trades ------------------------------------------
def test_04_05_crossing_sell_produces_a_trade(client):
    place(client, "BUY", 10, price=100)
    report = place(client, "SELL", 10, price=100)

    assert report["status"] == "FILLED"
    assert report["filled_quantity"] == 10
    assert len(report["trades"]) == 1

    trade = report["trades"][0]
    assert trade["price"] == 100
    assert trade["quantity"] == 10
    assert trade["buy_id"] == 1 and trade["sell_id"] == 2

    book = client.get("/book").json()
    assert book["bids"] == [] and book["asks"] == []


def test_05b_trade_appears_in_trade_history(client):
    place(client, "BUY", 10, price=100)
    place(client, "SELL", 10, price=100)

    trades = client.get("/trades").json()
    assert trades["total"] == 1
    assert trades["trades"][0]["quantity"] == 10


# --- 6: partial fill --------------------------------------------------------
def test_06_partial_fill_leaves_remainder_resting(client):
    place(client, "BUY", 10, price=100)
    report = place(client, "SELL", 25, price=100)

    assert report["status"] == "PARTIALLY_FILLED"
    assert report["filled_quantity"] == 10
    assert report["remaining_quantity"] == 15
    assert report["resting"] is True

    # The remainder became the new best ask.
    book = client.get("/book").json()
    assert book["asks"] == [{"price": 100, "quantity": 15}]
    assert book["bids"] == []


def test_06b_partial_fill_of_a_resting_order(client):
    buy = place(client, "BUY", 30, price=100)
    place(client, "SELL", 12, price=100)

    resting = client.get(f"/orders/{buy['order_id']}").json()
    assert resting["remaining_quantity"] == 18, "resting order decremented in place"
    assert resting["status"] == "RESTING"


# --- 7: full fill -----------------------------------------------------------
def test_07_full_fill_across_two_resting_orders(client):
    place(client, "SELL", 5, price=101)
    place(client, "SELL", 5, price=102)

    report = place(client, "BUY", 10, price=102)

    assert report["status"] == "FILLED"
    assert report["remaining_quantity"] == 0
    assert [t["price"] for t in report["trades"]] == [101, 102], "cheapest ask first"
    assert client.get("/book").json()["asks"] == []


# --- 8: FIFO at the same price ---------------------------------------------
def test_08_fifo_within_a_price_level(client):
    first = place(client, "BUY", 5, price=100)
    second = place(client, "BUY", 5, price=100)

    report = place(client, "SELL", 5, price=100)

    assert len(report["trades"]) == 1
    assert report["trades"][0]["buy_id"] == first["order_id"], "earliest order fills first"

    # The first order is gone; the second is untouched.
    assert client.get(f"/orders/{first['order_id']}").status_code == 404
    assert client.get(f"/orders/{second['order_id']}").json()["remaining_quantity"] == 5


def test_08b_price_priority_beats_time_priority(client):
    early_worse = place(client, "BUY", 5, price=99)
    late_better = place(client, "BUY", 5, price=101)

    report = place(client, "SELL", 5, price=99)

    assert report["trades"][0]["buy_id"] == late_better["order_id"]
    assert report["trades"][0]["price"] == 101, "traded at the resting bid's price"
    assert client.get(f"/orders/{early_worse['order_id']}").status_code == 200


# --- 9: cancellation --------------------------------------------------------
def test_09_cancel_removes_the_order(client):
    order = place(client, "BUY", 10, price=100)

    response = client.delete(f"/orders/{order['order_id']}")
    assert response.status_code == 200
    assert response.json() == {
        "order_id": order["order_id"],
        "cancelled": True,
        "status": "CANCELLED",
    }

    assert client.get("/book").json()["bids"] == []
    assert client.get("/stats").json()["active_orders"] == 0


def test_09b_cancelled_order_cannot_trade(client):
    order = place(client, "BUY", 10, price=100)
    client.delete(f"/orders/{order['order_id']}")

    report = place(client, "SELL", 10, price=100)

    assert report["trades"] == [], "nothing left to match against"
    assert report["status"] == "RESTING"


def test_09c_cancel_unknown_or_already_cancelled_is_404(client):
    order = place(client, "BUY", 10, price=100)

    assert client.delete(f"/orders/{order['order_id']}").status_code == 200
    assert client.delete(f"/orders/{order['order_id']}").status_code == 404
    assert client.delete("/orders/999999").status_code == 404


def test_09d_cancel_one_of_two_orders_at_a_price(client):
    first = place(client, "BUY", 5, price=100)
    place(client, "BUY", 7, price=100)

    client.delete(f"/orders/{first['order_id']}")

    book = client.get("/book").json()
    assert book["bids"] == [{"price": 100, "quantity": 7}], "level survives, quantity drops"


# --- 10-11: market orders ---------------------------------------------------
def test_10_market_buy_consumes_best_asks(client):
    place(client, "SELL", 5, price=101)
    place(client, "SELL", 5, price=102)

    report = place(client, "BUY", 7, order_type="MARKET")

    assert report["status"] == "FILLED"
    assert report["type"] == "MARKET"
    assert report["price"] is None
    assert [(t["price"], t["quantity"]) for t in report["trades"]] == [(101, 5), (102, 2)]
    assert all(t["buy_id"] == -1 for t in report["trades"]), "market side has no resting id"

    assert client.get("/book").json()["best_ask"] == 102


def test_11_market_sell_consumes_best_bids(client):
    place(client, "BUY", 5, price=100)
    place(client, "BUY", 5, price=99)

    report = place(client, "SELL", 8, order_type="MARKET")

    assert report["status"] == "FILLED"
    assert [(t["price"], t["quantity"]) for t in report["trades"]] == [(100, 5), (99, 3)]
    assert all(t["sell_id"] == -1 for t in report["trades"])

    assert client.get("/book").json()["best_bid"] == 99


def test_11b_market_order_never_rests_its_remainder(client):
    place(client, "SELL", 3, price=101)

    report = place(client, "BUY", 10, order_type="MARKET")

    assert report["filled_quantity"] == 3
    assert report["remaining_quantity"] == 7
    assert report["status"] == "PARTIALLY_FILLED"
    assert report["resting"] is False
    assert client.get("/stats").json()["active_orders"] == 0, "remainder discarded"


def test_11c_market_order_into_an_empty_book_is_rejected(client):
    report = place(client, "BUY", 10, order_type="MARKET")

    assert report["status"] == "REJECTED"
    assert report["filled_quantity"] == 0
    assert report["trades"] == []


# --- validation & plumbing --------------------------------------------------
def test_health_reports_a_live_engine(client):
    body = client.get("/health").json()
    assert body["status"] == "ok"
    assert body["engine_alive"] is True


def test_empty_book_reports_null_not_minus_one(client):
    book = client.get("/book").json()
    assert book["best_bid"] is None, "the engine's -1 sentinel must not leak"
    assert book["best_ask"] is None
    assert book["spread"] is None


def test_spread_is_null_when_one_side_is_empty(client):
    place(client, "BUY", 5, price=100)
    book = client.get("/book").json()
    assert book["best_bid"] == 100
    assert book["best_ask"] is None
    assert book["spread"] is None


def test_stats_track_the_engine(client):
    place(client, "BUY", 10, price=100)
    place(client, "SELL", 4, price=100)

    stats = client.get("/stats").json()
    assert stats["orders_processed"] == 2
    assert stats["trades_executed"] == 1
    assert stats["active_orders"] == 1
    assert stats["best_bid"] == 100


def test_order_ids_are_unique_and_monotonic(client):
    ids = [
        place(client, "BUY", 1, price=100)["order_id"],
        place(client, "BUY", 1, order_type="MARKET")["order_id"],
        place(client, "SELL", 1, price=105)["order_id"],
    ]
    assert ids == sorted(ids)
    assert len(set(ids)) == 3


def test_invalid_orders_are_rejected_before_reaching_the_engine(client):
    cases = [
        {"side": "BUY", "type": "LIMIT", "quantity": 10},                 # no price
        {"side": "BUY", "type": "MARKET", "price": 100, "quantity": 10},  # price on market
        {"side": "BUY", "type": "LIMIT", "price": 100, "quantity": 0},    # zero qty
        {"side": "BUY", "type": "LIMIT", "price": -5, "quantity": 10},    # negative price
        {"side": "SIDEWAYS", "type": "LIMIT", "price": 100, "quantity": 1},
        {"side": "BUY", "type": "STOP", "price": 100, "quantity": 1},
    ]
    for body in cases:
        assert client.post("/orders", json=body).status_code == 422, body


def test_get_order_404_once_filled(client):
    order = place(client, "BUY", 5, price=100)
    place(client, "SELL", 5, price=100)

    # The engine drops filled orders from its lookup map by design.
    assert client.get(f"/orders/{order['order_id']}").status_code == 404


def test_book_depth_parameter_limits_levels(client):
    for price in (100, 99, 98, 97):
        place(client, "BUY", 5, price=price)

    assert len(client.get("/book?depth=2").json()["bids"]) == 2
    assert len(client.get("/book").json()["bids"]) == 4


def test_trades_limit_returns_the_most_recent(client):
    place(client, "BUY", 100, price=100)
    for _ in range(5):
        place(client, "SELL", 1, price=100)

    body = client.get("/trades?limit=2").json()
    assert body["total"] == 5
    assert len(body["trades"]) == 2
    assert [t["seq"] for t in body["trades"]] == [3, 4], "the two most recent"
