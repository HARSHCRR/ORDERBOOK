"""
Tests for the C++ side itself.

Two jobs:

  1. run the engine's own matching-semantics tests (engine/engine_tests.cpp);
  2. prove the ORIGINAL, untouched benchmark sources still compile and run
     independently of everything added around them. If the full-stack work ever
     breaks `g++ -O2 "list<>.cpp"`, the README's performance claims stop being
     reproducible and these tests say so.

The benchmark itself is NOT run here -- it processes 10M orders and belongs in
`make bench`, not in a test suite.
"""

import json
import subprocess
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD = REPO_ROOT / "build"


def test_cpp_matching_tests_pass():
    """engine/engine_tests.cpp -- the semantics the API layer depends on."""
    subprocess.run(["make", "build/engine_tests"], cwd=REPO_ROOT, check=True,
                   capture_output=True)

    result = subprocess.run([str(BUILD / "engine_tests")], capture_output=True, text=True)

    assert result.returncode == 0, result.stdout
    assert "ALL ENGINE TESTS PASSED" in result.stdout


@pytest.mark.parametrize("source", ["list<>.cpp", "multithreading.cpp"])
def test_original_programs_still_link(source, tmp_path):
    """
    The original standalone PROGRAMS are untouched and must stay buildable
    exactly as the README has always described.
    """
    assert (REPO_ROOT / source).exists(), f"{source} is missing from the repo"

    result = subprocess.run(
        ["g++", "-O2", "-std=c++17", "-o", str(tmp_path / "out"), source],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr


@pytest.mark.parametrize("source", ["deque<>.cpp", "c.cpp", "level2.cpp", "testlevel2.cpp"])
def test_original_sources_still_compile(source):
    """
    Syntax-only, because not every original file is a linkable program.

    Notably `deque<>.cpp` defines the OrderBook class and STOPS -- it has no
    main(). That is pre-existing: the original README's
    `g++ -O2 -o orderbook_deque "deque<>.cpp"` has never linked. The file is
    left exactly as it was rather than silently given a main(); this test just
    pins down that it is still valid C++17.
    """
    result = subprocess.run(
        ["g++", "-O2", "-std=c++17", "-fsyntax-only", source],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr


def test_extracted_header_benchmark_builds_in_benchmark_mode():
    """
    The benchmark binary must compile with -DORDERBOOK_BENCHMARK, which is what
    removes trade recording and reproduces the configuration the 15.33M
    orders/sec figure was measured in.
    """
    subprocess.run(["make", "build/benchmark"], cwd=REPO_ROOT, check=True,
                   capture_output=True)
    assert (BUILD / "benchmark").exists()


def test_engine_process_speaks_ndjson_directly():
    """
    Drive the engine by hand, with no Python bridge involved, to prove the
    protocol is debuggable straight from a terminal.
    """
    subprocess.run(["make", "build/engine_server"], cwd=REPO_ROOT, check=True,
                   capture_output=True)

    commands = [
        {"action": "PLACE_ORDER", "side": "BUY", "type": "LIMIT", "price": 100, "quantity": 10},
        {"action": "PLACE_ORDER", "side": "SELL", "type": "LIMIT", "price": 100, "quantity": 4},
        {"action": "GET_ORDER_BOOK"},
        {"action": "SHUTDOWN"},
    ]

    result = subprocess.run(
        [str(BUILD / "engine_server")],
        input="\n".join(json.dumps(c) for c in commands) + "\n",
        capture_output=True,
        text=True,
    )

    responses = [json.loads(line) for line in result.stdout.strip().splitlines()]

    assert responses[0]["status"] == "RESTING"
    assert responses[1]["status"] == "FILLED"
    assert responses[1]["trades"] == [
        {"seq": 0, "buy_id": 1, "sell_id": 2, "price": 100, "quantity": 4}
    ]
    assert responses[2]["bids"] == [{"price": 100, "quantity": 6}]
    assert responses[3]["shutdown"] is True


def test_engine_rejects_bad_input_without_dying():
    """A malformed line must not kill the process or corrupt the book."""
    subprocess.run(["make", "build/engine_server"], cwd=REPO_ROOT, check=True,
                   capture_output=True)

    payload = (
        '{"action":"PLACE_ORDER","side":"BUY","type":"LIMIT","price":100,"quantity":5}\n'
        "this is not json\n"
        '{"action":"PLACE_ORDER","side":"UPWARDS","type":"LIMIT","price":1,"quantity":1}\n'
        '{"action":"GET_ORDER_BOOK"}\n'
        '{"action":"SHUTDOWN"}\n'
    )

    result = subprocess.run(
        [str(BUILD / "engine_server")], input=payload, capture_output=True, text=True
    )

    responses = [json.loads(line) for line in result.stdout.strip().splitlines()]

    assert responses[1]["error"] == "BAD_JSON"
    assert responses[2]["error"] == "BAD_SIDE"
    assert responses[3]["bids"] == [{"price": 100, "quantity": 5}], "book survived"
    assert result.returncode == 0
