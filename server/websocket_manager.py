"""
websocket_manager.py -- fan-out of engine events to connected browsers.

This is deliberately the simplest thing that works: a set of live WebSocket
connections and a broadcast() that writes the same JSON to each one.

There is no pub/sub broker, no per-client subscription filtering and no message
queue.  There does not need to be: the engine is a single writer, every event
is small, and every connected client wants every event.  Adding a broker here
would be architecture for its own sake.

Two properties worth knowing:

  * Broadcast is best-effort.  A client that has gone away is dropped rather
    than retried -- a dead browser tab must never be able to stall the engine.
  * Events are emitted AFTER the engine has applied the command, so what a
    client receives is always a state the engine actually passed through.
"""

import asyncio
import logging
from typing import Any, Dict, List, Set

from fastapi import WebSocket

log = logging.getLogger("websocket_manager")


class WebSocketManager:
    def __init__(self) -> None:
        self._connections: Set[WebSocket] = set()
        self._lock = asyncio.Lock()

    @property
    def connection_count(self) -> int:
        return len(self._connections)

    async def connect(self, websocket: WebSocket) -> None:
        await websocket.accept()
        async with self._lock:
            self._connections.add(websocket)
        log.info("websocket connected (%d total)", len(self._connections))

    async def disconnect(self, websocket: WebSocket) -> None:
        async with self._lock:
            self._connections.discard(websocket)
        log.info("websocket disconnected (%d total)", len(self._connections))

    async def broadcast(self, message: Dict[str, Any]) -> None:
        """Send one event to every connected client, dropping the dead ones."""
        async with self._lock:
            targets: List[WebSocket] = list(self._connections)

        if not targets:
            return

        dead: List[WebSocket] = []
        for ws in targets:
            try:
                await ws.send_json(message)
            except Exception:
                # Client vanished mid-send. Not an error worth logging loudly.
                dead.append(ws)

        if dead:
            async with self._lock:
                for ws in dead:
                    self._connections.discard(ws)

    # -- the three event shapes the frontend understands --------------------
    async def broadcast_book(self, book: Dict[str, Any]) -> None:
        await self.broadcast({"event": "BOOK_UPDATE", "book": book})

    async def broadcast_trade(self, trade: Dict[str, Any]) -> None:
        await self.broadcast({"event": "TRADE", "trade": trade})

    async def broadcast_stats(self, stats: Dict[str, Any]) -> None:
        await self.broadcast({"event": "STATS", "stats": stats})
