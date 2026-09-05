"""
Shared pytest fixtures.

Every test gets a FRESH engine process. The FastAPI lifespan starts one
`engine_server` on enter and shuts it down on exit, so wrapping each test in
`with TestClient(app)` gives it an empty order book with order ids restarting
at 1. That isolation matters here: the engine deliberately keeps no way to
reset itself, because a real exchange does not have one.
"""

import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent
SERVER_DIR = REPO_ROOT / "server"
ENGINE_BINARY = REPO_ROOT / "build" / "engine_server"

sys.path.insert(0, str(SERVER_DIR))


@pytest.fixture(scope="session", autouse=True)
def built_engine():
    """Build the C++ side once before any test runs."""
    if not ENGINE_BINARY.exists():
        subprocess.run(["make", "all"], cwd=REPO_ROOT, check=True)
    assert ENGINE_BINARY.exists(), f"engine binary missing at {ENGINE_BINARY}"
    return ENGINE_BINARY


@pytest.fixture()
def client(built_engine):
    """A TestClient with its own freshly-spawned C++ engine."""
    from fastapi.testclient import TestClient

    from main import app

    with TestClient(app) as test_client:
        yield test_client


# ---------------------------------------------------------------------------
# Helpers used across the API tests.
# ---------------------------------------------------------------------------
def place(client, side, quantity, price=None, order_type=None):
    """POST /orders and return the execution report, failing loudly on error."""
    body = {
        "side": side,
        "type": order_type or ("MARKET" if price is None else "LIMIT"),
        "quantity": quantity,
    }
    if price is not None:
        body["price"] = price

    response = client.post("/orders", json=body)
    assert response.status_code == 201, response.text
    return response.json()
