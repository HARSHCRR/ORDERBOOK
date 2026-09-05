"""
WebSocket tests -- items 12 and 13 of the checklist.

The point of these is that the frontend does NOT poll: a state change made over
HTTP has to arrive on an already-open WebSocket without the client asking.
"""

from conftest import place


def drain(websocket, count, timeout_events=12):
    """Read up to `timeout_events` messages, returning the first `count` real events."""
    received = []
    for _ in range(timeout_events):
        received.append(websocket.receive_json())
        if len(received) >= count:
            break
    return received


def test_12_snapshot_on_connect(client):
    place(client, "BUY", 10, price=100)
    place(client, "SELL", 5, price=101)

    with client.websocket_connect("/ws") as websocket:
        message = websocket.receive_json()

    assert message["event"] == "SNAPSHOT"
    assert message["book"]["bids"] == [{"price": 100, "quantity": 10}]
    assert message["book"]["asks"] == [{"price": 101, "quantity": 5}]
    assert message["stats"]["active_orders"] == 2
    assert message["trades"] == []


def test_12b_book_update_is_pushed_after_an_http_order(client):
    with client.websocket_connect("/ws") as websocket:
        assert websocket.receive_json()["event"] == "SNAPSHOT"

        place(client, "BUY", 10, price=100)

        events = drain(websocket, 2)

    by_type = {e["event"]: e for e in events}
    assert "BOOK_UPDATE" in by_type, f"got {[e['event'] for e in events]}"
    assert by_type["BOOK_UPDATE"]["book"]["bids"] == [{"price": 100, "quantity": 10}]
    assert by_type["BOOK_UPDATE"]["book"]["best_bid"] == 100


def test_13_trade_event_is_pushed(client):
    place(client, "BUY", 10, price=100)

    with client.websocket_connect("/ws") as websocket:
        assert websocket.receive_json()["event"] == "SNAPSHOT"

        place(client, "SELL", 10, price=100)

        events = drain(websocket, 3)

    kinds = [e["event"] for e in events]
    assert "TRADE" in kinds, f"got {kinds}"

    trade = next(e for e in events if e["event"] == "TRADE")["trade"]
    assert trade["price"] == 100
    assert trade["quantity"] == 10

    # The trade must be announced before the book that reflects it.
    assert kinds.index("TRADE") < kinds.index("BOOK_UPDATE")


def test_13b_stats_event_is_pushed(client):
    with client.websocket_connect("/ws") as websocket:
        websocket.receive_json()
        place(client, "BUY", 10, price=100)
        events = drain(websocket, 2)

    stats = next((e for e in events if e["event"] == "STATS"), None)
    assert stats is not None, f"got {[e['event'] for e in events]}"
    assert stats["stats"]["orders_processed"] == 1


def test_13c_cancellation_is_broadcast(client):
    order = place(client, "BUY", 10, price=100)

    with client.websocket_connect("/ws") as websocket:
        websocket.receive_json()
        client.delete(f"/orders/{order['order_id']}")
        events = drain(websocket, 2)

    book_update = next(e for e in events if e["event"] == "BOOK_UPDATE")
    assert book_update["book"]["bids"] == [], "cancellation cleared the level"


def test_13d_two_clients_both_receive_the_same_event(client):
    """Broadcast really fans out, rather than only reaching the last connection."""
    with client.websocket_connect("/ws") as first, client.websocket_connect("/ws") as second:
        assert first.receive_json()["event"] == "SNAPSHOT"
        assert second.receive_json()["event"] == "SNAPSHOT"

        place(client, "BUY", 7, price=100)

        first_events = drain(first, 2)
        second_events = drain(second, 2)

    for events in (first_events, second_events):
        update = next(e for e in events if e["event"] == "BOOK_UPDATE")
        assert update["book"]["bids"] == [{"price": 100, "quantity": 7}]


def test_ping_pong(client):
    with client.websocket_connect("/ws") as websocket:
        websocket.receive_json()
        websocket.send_text("ping")
        assert websocket.receive_json() == {"event": "PONG"}
