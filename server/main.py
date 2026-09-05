"""
main.py -- the FastAPI layer.

Its job is orchestration, nothing else.  Specifically it does NOT:

  * keep its own copy of the order book,
  * compute best bid / best ask / spread,
  * assign order ids,
  * decide what matched.

All of that comes from the C++ engine on every single request.  If this file
ever starts caching book state, the engine has stopped being the source of
truth and the whole design argument falls apart.

Request flow for a state-changing call:

    HTTP POST /orders
        -> pydantic validation
        -> EngineBridge.place_order()      (asyncio.Lock, one command at a time)
        -> C++ engine matches
        -> response returned to the caller
        -> _broadcast_state() pushes TRADE + BOOK_UPDATE + STATS over WS
"""

import logging
from contextlib import asynccontextmanager
from typing import Any, Dict

from fastapi import FastAPI, HTTPException, Query, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware

from engine_bridge import EngineBridge, EngineError
from models import (
    BookResponse,
    CancelResponse,
    HealthResponse,
    OrderResponse,
    PlaceOrderRequest,
    PlaceOrderResponse,
    StatsResponse,
    TradesResponse,
)
from websocket_manager import WebSocketManager

logging.basicConfig(level=logging.INFO, format="%(levelname)s %(name)s: %(message)s")
log = logging.getLogger("exchange")

bridge = EngineBridge()
ws_manager = WebSocketManager()

# How many price levels the UI shows per side.
BOOK_DEPTH = 15


@asynccontextmanager
async def lifespan(app: FastAPI):
    """One engine process for the lifetime of the server."""
    await bridge.start()
    log.info("C++ engine started: %s", bridge.binary_path)
    try:
        yield
    finally:
        await bridge.stop()
        log.info("C++ engine stopped")


app = FastAPI(
    title="Low-Latency Trading Exchange",
    description="FastAPI orchestration layer over a C++17 price-time-priority matching engine.",
    version="1.0.0",
    lifespan=lifespan,
)

# The Vite dev server runs on a different port, so the browser needs CORS.
app.add_middleware(
    CORSMiddleware,
    allow_origins=[
        "http://localhost:5173",
        "http://127.0.0.1:5173",
        "http://localhost:4173",
    ],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)


def _engine_error(exc: EngineError) -> HTTPException:
    """Engine validation failures are client errors; anything else is a 503."""
    if exc.code in {"ENGINE_DOWN", "ENGINE_MISSING", "BAD_ENGINE_RESPONSE"}:
        return HTTPException(status_code=503, detail=str(exc))
    return HTTPException(status_code=400, detail=str(exc))


async def _broadcast_state(new_trades: Any = None) -> None:
    """
    Push the post-command state to every connected client.

    Called only after the engine has applied a command, so clients never see a
    book that the engine did not actually produce.  Individual TRADE events go
    first so a trade tape stays in execution order, then the aggregated book,
    then the counters.

    With no listeners this returns immediately.  That matters: building a
    broadcast costs two extra engine round-trips (GET_ORDER_BOOK + GET_STATS)
    on top of the order itself, and every one of them holds the bridge lock
    that real orders are queued behind.  Doing that work for nobody would
    triple the cost of an order placed over plain HTTP.
    """
    if ws_manager.connection_count == 0:
        return

    try:
        for trade in new_trades or []:
            await ws_manager.broadcast_trade(trade)

        book = await bridge.get_book(BOOK_DEPTH)
        await ws_manager.broadcast_book(book)

        stats = await bridge.get_stats()
        await ws_manager.broadcast_stats(stats)
    except EngineError as exc:
        # A broadcast failure must not fail the caller's already-executed order.
        log.warning("broadcast skipped: %s", exc)


# ---------------------------------------------------------------------------
# REST
# ---------------------------------------------------------------------------
@app.get("/health", response_model=HealthResponse)
async def health() -> Dict[str, Any]:
    return {
        "status": "ok" if bridge.alive else "degraded",
        "engine_alive": bridge.alive,
        "engine_binary": str(bridge.binary_path),
    }


@app.get("/book", response_model=BookResponse)
async def get_book(depth: int = Query(default=BOOK_DEPTH, ge=1, le=200)):
    try:
        return await bridge.get_book(depth)
    except EngineError as exc:
        raise _engine_error(exc)


@app.get("/trades", response_model=TradesResponse)
async def get_trades(limit: int = Query(default=50, ge=1, le=1000)):
    try:
        return await bridge.get_trades(limit)
    except EngineError as exc:
        raise _engine_error(exc)


@app.get("/stats", response_model=StatsResponse)
async def get_stats():
    try:
        return await bridge.get_stats()
    except EngineError as exc:
        raise _engine_error(exc)


@app.post("/orders", response_model=PlaceOrderResponse, status_code=201)
async def place_order(order: PlaceOrderRequest):
    """
    Submit an order. The engine matches it synchronously and the execution
    report below is derived entirely from what the engine did.
    """
    try:
        result = await bridge.place_order(
            side=order.side.value,
            order_type=order.type.value,
            quantity=order.quantity,
            price=order.price,
        )
    except EngineError as exc:
        raise _engine_error(exc)

    await _broadcast_state(result.get("trades"))
    return result


@app.delete("/orders/{order_id}", response_model=CancelResponse)
async def cancel_order(order_id: int):
    try:
        result = await bridge.cancel_order(order_id)
    except EngineError as exc:
        raise _engine_error(exc)

    if not result.get("cancelled"):
        # Not on the book: already filled, already cancelled, or never existed.
        raise HTTPException(status_code=404, detail=f"order {order_id} is not resting")

    await _broadcast_state()
    return result


@app.get("/orders/{order_id}", response_model=OrderResponse)
async def get_order(order_id: int):
    """
    Look up a RESTING order.

    The engine drops an order from its lookup map the moment it is fully filled
    or cancelled -- it keeps no per-order archive -- so a 404 here means
    'not currently on the book', not 'never existed'.
    """
    try:
        result = await bridge.get_order(order_id)
    except EngineError as exc:
        raise _engine_error(exc)

    if not result.get("resting"):
        raise HTTPException(status_code=404, detail=f"order {order_id} is not resting")

    return result


# ---------------------------------------------------------------------------
# WebSocket
# ---------------------------------------------------------------------------
@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    """
    Real-time feed.

    On connect the client is sent a full snapshot so it can render immediately
    without an extra HTTP call; after that it receives only deltas as they
    happen. The receive loop exists purely to detect disconnects -- the server
    ignores whatever a client sends (except "ping").
    """
    await ws_manager.connect(websocket)

    try:
        book = await bridge.get_book(BOOK_DEPTH)
        trades = await bridge.get_trades(50)
        stats = await bridge.get_stats()
        await websocket.send_json(
            {
                "event": "SNAPSHOT",
                "book": book,
                "trades": trades.get("trades", []),
                "stats": stats,
            }
        )
    except EngineError as exc:
        log.warning("snapshot failed on connect: %s", exc)

    try:
        while True:
            message = await websocket.receive_text()
            if message == "ping":
                await websocket.send_json({"event": "PONG"})
    except WebSocketDisconnect:
        pass
    except Exception as exc:
        log.info("websocket closed: %s", exc)
    finally:
        await ws_manager.disconnect(websocket)
