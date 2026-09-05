"""
Concurrency tests for the bridge.

The engine is single-threaded, so the bridge serialises every command behind
one asyncio.Lock. These tests exercise the CONTENDED path, which is the one
that used to be broken: an asyncio.Lock constructed outside a running event
loop works fine while uncontended and only fails once two requests overlap.
"""

import asyncio
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "server"))

from engine_bridge import EngineBridge  # noqa: E402

# Constructed HERE, at import time, with no event loop running -- deliberately
# mirroring how server/main.py creates its module-level `bridge`. This is the
# construction order that used to break: an asyncio.Lock built outside a loop
# binds to the wrong one on Python 3.9 and only fails once contended. Building
# the bridge inside an async test would bind the lock correctly and hide it.
_IMPORT_TIME_BRIDGE = EngineBridge()


@pytest.mark.asyncio
async def test_contended_commands_on_an_import_time_bridge(built_engine):
    """
    Regression guard for the loop-binding bug.

    Uses the bridge built at module import time (see above) and forces genuine
    contention. Before the fix this raised
    "got Future attached to a different loop" from asyncio.Lock.acquire().
    """
    bridge = _IMPORT_TIME_BRIDGE
    await bridge.start()
    try:
        results = await asyncio.gather(
            *[bridge.place_order("BUY", "LIMIT", 1, 100) for _ in range(50)]
        )
        assert len({r["order_id"] for r in results}) == 50
        assert (await bridge.get_stats())["orders_processed"] == 50
    finally:
        await bridge.stop()


@pytest.mark.asyncio
async def test_contended_commands_all_succeed(built_engine):
    """50 overlapping commands must all be applied exactly once, in lock order."""
    bridge = EngineBridge()
    await bridge.start()
    try:
        results = await asyncio.gather(
            *[bridge.place_order("BUY", "LIMIT", 1, 100) for _ in range(50)]
        )

        ids = [r["order_id"] for r in results]
        assert len(set(ids)) == 50, "every order got a distinct id"

        stats = await bridge.get_stats()
        assert stats["orders_processed"] == 50
        assert stats["active_orders"] == 50

        book = await bridge.get_book()
        assert book["bids"] == [{"price": 100, "quantity": 50}]
    finally:
        await bridge.stop()


@pytest.mark.asyncio
async def test_concurrent_orders_match_deterministically(built_engine):
    """
    Interleaved buys and sells must produce a book consistent with SOME serial
    order -- never a torn or double-counted one. Total resting quantity plus
    twice the traded quantity has to equal everything submitted.
    """
    bridge = EngineBridge()
    await bridge.start()
    try:
        commands = []
        for _ in range(25):
            commands.append(bridge.place_order("BUY", "LIMIT", 2, 100))
            commands.append(bridge.place_order("SELL", "LIMIT", 2, 100))

        await asyncio.gather(*commands)

        book = await bridge.get_book()
        stats = await bridge.get_stats()

        resting = sum(l["quantity"] for l in book["bids"] + book["asks"])
        trades = await bridge.get_trades(limit=1000)
        traded = sum(t["quantity"] for t in trades["trades"])

        submitted = 50 * 2
        assert resting + 2 * traded == submitted, "no quantity created or lost"
        assert stats["orders_processed"] == 50
    finally:
        await bridge.stop()


@pytest.mark.asyncio
async def test_bridge_reports_a_dead_engine(built_engine):
    """Killing the engine must surface as an error, not a hang."""
    from engine_bridge import EngineError

    bridge = EngineBridge()
    await bridge.start()

    bridge._proc.kill()          # noqa: SLF001 - deliberately simulating a crash
    await bridge._proc.wait()    # noqa: SLF001

    with pytest.raises(EngineError) as excinfo:
        await bridge.place_order("BUY", "LIMIT", 1, 100)

    assert excinfo.value.code == "ENGINE_DOWN"


@pytest.mark.asyncio
async def test_engine_survives_a_malformed_command(built_engine):
    """A bad command is rejected without taking the book down with it."""
    from engine_bridge import EngineError

    bridge = EngineBridge()
    await bridge.start()
    try:
        await bridge.place_order("BUY", "LIMIT", 10, 100)

        with pytest.raises(EngineError):
            await bridge.send({"action": "NONSENSE"})

        # The book is intact and the process is still answering.
        book = await bridge.get_book()
        assert book["bids"] == [{"price": 100, "quantity": 10}]
    finally:
        await bridge.stop()
