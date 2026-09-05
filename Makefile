# ---------------------------------------------------------------------------
# Build for the C++ side of the project.
#
#   make            -> engine_server, engine_tests, benchmark
#   make test       -> build + run the engine unit tests
#   make bench      -> the extracted-header benchmark (10M orders)
#   make bench-orig -> the ORIGINAL, untouched list<>.cpp benchmark
#   make clean
#
# Everything lands in build/.  Nothing here modifies the original top-level
# .cpp files; they still compile exactly as the README always described.
# ---------------------------------------------------------------------------

CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17
BUILD    := build

.PHONY: all test bench bench-orig check-deque clean

all: $(BUILD)/engine_server $(BUILD)/engine_tests $(BUILD)/benchmark

$(BUILD):
	mkdir -p $(BUILD)

# The process FastAPI talks to.  Trade recording ON.
$(BUILD)/engine_server: engine/engine_server.cpp engine/OrderBook.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ engine/engine_server.cpp

$(BUILD)/engine_tests: engine/engine_tests.cpp engine/OrderBook.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Wall -o $@ engine/engine_tests.cpp

# -DORDERBOOK_BENCHMARK compiles out trade recording and the order counter,
# reproducing the configuration list<>.cpp was measured in.
$(BUILD)/benchmark: engine/benchmark.cpp engine/OrderBook.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -DORDERBOOK_BENCHMARK -o $@ engine/benchmark.cpp

# NOTE: deque<>.cpp defines the OrderBook class only -- it has no main() and
# therefore cannot be linked into a program. (The original README claimed it
# could; it never has.) Syntax-check it instead of pretending otherwise.
check-deque:
	$(CXX) $(CXXFLAGS) -fsyntax-only "deque<>.cpp" && echo "deque<>.cpp: OK (class only, no main)"

test: $(BUILD)/engine_tests
	./$(BUILD)/engine_tests

bench: $(BUILD)/benchmark
	./$(BUILD)/benchmark

# The ORIGINAL benchmark, from the untouched list<>.cpp at the repo root,
# built exactly as the README has always described. This is the run the
# published 15.33M orders/sec figure comes from.
# (Declared phony rather than as a file rule because make cannot express a
# prerequisite whose name contains "<>".)
bench-orig: | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $(BUILD)/orderbook_list "list<>.cpp"
	./$(BUILD)/orderbook_list

clean:
	rm -rf $(BUILD)
