"""
engine_bridge.py -- the ONLY thing in this codebase that talks to C++.

Design
------
The C++ matching engine is single-threaded on purpose: that is what makes it
fast and what makes its behaviour deterministic.  So the bridge does not try to
parallelise it.  It:

  * spawns ONE long-lived `engine_server` process,
  * writes one JSON line to its stdin,
  * reads exactly one JSON line back from its stdout,
  * and holds an asyncio.Lock across that write/read pair.

The lock is the whole concurrency story.  It turns any number of concurrent
HTTP/WebSocket callers into a single ordered stream of commands, which is
exactly the input the engine was designed for.  Because commands are applied in
lock order, the engine's price-time priority is preserved all the way up to the
API: two clients racing to hit the same resting order get the same outcome they
would have got by calling the C++ engine directly, in that order.

Note this is a correctness decision, not a performance one.  It is also why the
README separates "engine throughput" from "end-to-end API throughput": every
HTTP request pays a process round-trip plus JSON encode/decode on both sides.

Why a subprocess and not pybind11 / ctypes?
-------------------------------------------
  * The engine keeps its own `main()`-style ownership of the book; nothing about
    it had to change to be driven this way.
  * A crash in C++ cannot take the web server down -- the bridge notices EOF.
  * The protocol is newline-delimited JSON, so the engine can be driven by hand
    from a terminal, which makes debugging trivial.
The cost is one pipe round-trip per command, which is tens of microseconds --
irrelevant next to HTTP.
"""

import asyncio
import json
import os
import shutil
from pathlib import Path
from typing import Any, Dict, Optional

# server/ -> repo root
REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BINARY = REPO_ROOT / "build" / "engine_server"


class EngineError(RuntimeError):
    """The engine replied with ok:false, or could not be reached at all."""

    def __init__(self, code: str, message: str):
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message


class EngineBridge:
    """Owns the C++ engine subprocess and the request/response protocol."""

    def __init__(self, binary_path: Optional[str] = None):
        self.binary_path = Path(
            binary_path or os.environ.get("ENGINE_BINARY") or DEFAULT_BINARY
        )
        self._proc: Optional[asyncio.subprocess.Process] = None
        # Created in start(), NOT here.  On Python 3.9 an asyncio.Lock binds to
        # whatever loop exists when it is constructed, and this object is built
        # at module import time -- before uvicorn's loop exists.  A lock bound
        # to the wrong loop works fine while uncontended and then raises
        # "got Future attached to a different loop" the first time two requests
        # actually overlap, which is precisely the case it exists to handle.
        # Building it inside start() guarantees it belongs to the running loop.
        self._lock: Optional[asyncio.Lock] = None
        self._next_id = 0

    # -- lifecycle ----------------------------------------------------------
    async def start(self) -> None:
        # Bind the lock to the loop that will actually serve requests.
        self._lock = asyncio.Lock()

        if not self.binary_path.exists():
            raise EngineError(
                "ENGINE_MISSING",
                f"engine binary not found at {self.binary_path}. Run `make` in the repo root.",
            )

        self._proc = await asyncio.create_subprocess_exec(
            str(self.binary_path),
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            # engine_server writes only diagnostics to stderr; let them land in
            # the uvicorn console rather than filling a pipe nobody drains.
            stderr=None,
        )

        # Prove the process is actually answering before we accept traffic.
        await self.send({"action": "PING"})

    async def stop(self) -> None:
        if self._proc is None:
            return
        proc, self._proc = self._proc, None
        self._lock = None
        try:
            if proc.returncode is None and proc.stdin is not None:
                proc.stdin.write(b'{"action":"SHUTDOWN"}\n')
                await proc.stdin.drain()
                await asyncio.wait_for(proc.wait(), timeout=3.0)
        except (asyncio.TimeoutError, ConnectionResetError, BrokenPipeError):
            proc.kill()
            await proc.wait()

    @property
    def alive(self) -> bool:
        return self._proc is not None and self._proc.returncode is None

    # -- the protocol -------------------------------------------------------
    async def send(self, command: Dict[str, Any]) -> Dict[str, Any]:
        """
        Send one command, return the engine's response dict.

        Serialised by `self._lock`: the write and the matching read are one
        atomic step, so responses can never be interleaved between callers.
        """
        if self._proc is None or self._proc.returncode is not None:
            raise EngineError("ENGINE_DOWN", "engine process is not running")

        if self._lock is None:
            raise EngineError("ENGINE_DOWN", "bridge was never started")

        async with self._lock:
            self._next_id += 1
            command = dict(command, id=self._next_id)

            assert self._proc.stdin is not None and self._proc.stdout is not None

            try:
                self._proc.stdin.write((json.dumps(command) + "\n").encode())
                await self._proc.stdin.drain()
            except (BrokenPipeError, ConnectionResetError) as exc:
                raise EngineError("ENGINE_DOWN", f"write failed: {exc}") from exc

            raw = await self._proc.stdout.readline()

            if not raw:
                # EOF: the C++ process died. Surface it instead of hanging.
                raise EngineError("ENGINE_DOWN", "engine closed its stdout (process died)")

            try:
                response = json.loads(raw.decode())
            except json.JSONDecodeError as exc:
                raise EngineError("BAD_ENGINE_RESPONSE", f"{exc}: {raw!r}") from exc

        if not response.get("ok", False):
            raise EngineError(
                response.get("error", "ENGINE_ERROR"),
                response.get("message", "engine reported a failure"),
            )

        # Strip protocol plumbing; callers only care about the payload.
        response.pop("ok", None)
        response.pop("id", None)
        return response

    # -- typed command helpers ---------------------------------------------
    async def place_order(
        self,
        side: str,
        order_type: str,
        quantity: int,
        price: Optional[int] = None,
    ) -> Dict[str, Any]:
        command: Dict[str, Any] = {
            "action": "PLACE_ORDER",
            "side": side,
            "type": order_type,
            "quantity": quantity,
        }
        if order_type == "LIMIT":
            command["price"] = price
        return await self.send(command)

    async def cancel_order(self, order_id: int) -> Dict[str, Any]:
        return await self.send({"action": "CANCEL_ORDER", "order_id": order_id})

    async def get_order(self, order_id: int) -> Dict[str, Any]:
        return await self.send({"action": "GET_ORDER", "order_id": order_id})

    async def get_book(self, depth: int = 15) -> Dict[str, Any]:
        return await self.send({"action": "GET_ORDER_BOOK", "depth": depth})

    async def get_trades(self, limit: int = 50) -> Dict[str, Any]:
        return await self.send({"action": "GET_TRADES", "limit": limit})

    async def get_stats(self) -> Dict[str, Any]:
        return await self.send({"action": "GET_STATS"})
