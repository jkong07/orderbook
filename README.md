# orderbook

A low-latency limit order book and market data feed handler, prototyped in Python and now ported to C++20.

## Why I built this

I like working at the level where memory layout and cache behavior actually decide performance instead of being an afterthought. An order book is a good excuse to build that muscle: start naive, measure everything, and optimize one change at a time.

## Features

What's actually implemented so far (Python prototype, phases 1-2 done):

- `Order` class — `order_id`, `side` (`Side` enum: `BUY`/`SELL`), `price` (integer cents), `qty` (mutable dataclass — resting orders are updated in place as they're matched)
- `OrderBook` class — bids and asks held as `SortedDict[price -> deque[Order]]`, with `_next_id()` as the single source of monotonic, unique order IDs
- `add()` — inserts a passive resting order at a given price level (does not itself check for crossing — callers are expected to match first, see `driver.py`)
- `cancel()` — removes a resting order by `order_id`, pruning the price level if it empties out; returns the removed `Order` (or `None` if not found)
- `execute()` — matches an incoming order against resting orders on the opposite side (price-time priority), reducing/removing resting orders as it fills; returns the list of fills as `(resting_order_id, price, qty)` tuples
- `depth(n=None)` — aggregates resting qty per price level for the top `n` bid/ask levels (all levels if `n` is omitted), returned as `(bid_levels, ask_levels)` price/qty tuples
- `printer(bid_list, ask_list)` — renders a `depth()` result as a two-column terminal table, asks above bids, best prices innermost
- `invariants.py` — read-only, free-function checkers that raise `InvariantViolation` (with the offending numbers in the message) on: crossed/locked book, non-positive resting qty, empty price levels, price/side mismatches between an order and its level, FIFO ordering within a level, duplicate order IDs across both books, and per-side conservation of qty (`added - cancelled - filled == resting`, reconciled separately for bids and asks)
- `driver.py` — seeded, reproducible random event generator (`random.Random(seed)`, seed printed every run) and driver loop: pre-generates a weighted mix of add/cancel/execute events (60/30/10) with prices clustered around a drifting reference so the book actually crosses and walks multiple levels, replays them against a fresh `OrderBook`, and calls every invariant checker after *each* event so failures point at the exact event that broke something

What's implemented in the C++ port (phase 3 done — naive baseline, not yet optimized):

- `orderbook::Order` (`include/orderbook/order.hpp`) — `order_id`, `side` (`Side` enum: `Buy`/`Sell`), `price`, `qty`, plain struct
- `orderbook::Fill` / `orderbook::ExecuteResult` (`include/orderbook/fill.hpp`) — `execute()`'s return type: the list of fills plus the incoming order's remaining unfilled qty, returned by value instead of mutating the caller's order in place
- `orderbook::OrderBook` (`include/orderbook/book.hpp`, `src/book.cpp`) — bids/asks held as distinct `std::map<Price, std::deque<Order>>` types (bids use `std::greater<Price>` so both sides iterate best-first from `begin()`); `next_id()`, `add()`, `cancel()`, `execute()`, `depth(n = nullopt)`, `printer()` — direct C++ translations of the Python API, see `SPEC.md` §4b for the full rationale behind each type/ownership decision
- GoogleTest suite (`tests/order_book_test.cpp`) — hand-verified sequences covering `Order` construction, empty-book behavior, `add`/`cancel`/`execute`/`depth`, same-price aggregation, multi-level walks, limit-price stops, and that `execute()` doesn't mutate the caller's order
- A differential test harness against the Python prototype was considered and deliberately cut — see `SPEC.md` §6 for why

What's implemented for real-data validation and benchmarking (phases 4-5 done):

- `orderbook::OrderBook::reduce()` (`book.hpp`/`book.cpp`) — shrinks a resting order in place by a given qty, preserving its queue position (unlike cancel-and-re-add); removes it entirely if the reduction consumes it
- `orderbook::lobster` (`include/orderbook/lobster.hpp`, `src/lobster.cpp`) — parses LOBSTER message-file rows and `apply()`s them to an `OrderBook` (New→`add()`, PartialCancel/ExecuteVisible→`reduce()`, Delete→`cancel()`, ExecuteHidden/Cross/Halt→no-op)
- `orderbook::lobster` validation helpers (`lobster_validate.hpp`/`.cpp`) — parses LOBSTER orderbook-snapshot rows and diffs them against `OrderBook::depth()`
- `orderbook::invariants` (`invariants.hpp`/`.cpp`) — ported from `python/invariants.py`: not-crossed, positive-qty, unique-IDs, and per-side conservation checks
- `tools/lobster_replay` — CLI that replays a full LOBSTER day against `OrderBook`, checking invariants after every message and diffing against published snapshots (run manually against the downloaded sample; see `SPEC.md` §4c for why exact snapshot matching isn't achievable for a full day with free LOBSTER samples — invariants are the full-day correctness bar instead)
- `tools/benchmark` — CLI that replays a LOBSTER day and reports per-message-type p50/p99/p99.9 latency and overall throughput; see `SPEC.md` §7 for methodology and results
- `tests/lobster_replay_test.cpp` + `tests/fixtures/` — a short real-data slice (12 messages) run in CI with exact snapshot matching, proving the parser/dispatch logic against genuine exchange data
- `tests/invariants_test.cpp` — unit coverage for each invariant check

## Status

- [x] **Phase 1** — Python prototype: add/cancel/execute logic, terminal depth-view renderer — `add()`, `cancel()`, `execute()`, `depth()`, `printer()`
- [x] **Phase 2** — Invariant checks (`invariants.py`: crossed book, non-positive qty, empty levels, price/side consistency, FIFO ordering, unique IDs, per-side conservation) + seeded randomized event fuzzing (`driver.py`), checked after every event
- [x] **Phase 3** — Port to C++: CMake, GoogleTest, GitHub Actions CI (macOS + Ubuntu), naive baseline with `std::map<Price, std::deque<Order>>`, hand-verified sequences ported to GoogleTest cases
- [x] **Phase 4** — Parse real Nasdaq ITCH 5.0 data via LOBSTER samples, replay it, validate against published snapshots. Free LOBSTER samples turned out to not support exact full-day reconstruction (windowed to trading hours, so pre-existing resting orders are invisible — see `SPEC.md` §4c); validation was scoped to invariants holding over the full real day (0 violations across 400,391 messages) plus exact snapshot matching over a short real window
- [x] **Phase 5** — Benchmark p50/p99/p99.9 latency and throughput — see results below and `SPEC.md` §7
- [ ] **Phase 6** — Optimize incrementally (array-indexed price levels → intrusive linked lists → O(1) cancel via hash map → memory pool → cache alignment), benchmarking after each change and recording results in a table

## How to run it

```bash
cd python
python3 -m venv .venv
source .venv/bin/activate
pip install sortedcontainers
```

Then, from a REPL (run from the `python/` directory):

```python
from book import Order, OrderBook, Side

book = OrderBook()

# order IDs come from the book's own counter, so they stay unique and monotonic
order = Order(book._next_id(), side=Side.BUY, price=10050, qty=100)
book.add(order)
book.cancel(order.order_id)  # returns the removed Order, or None if not found

# match an incoming order against the opposite side of the book
resting = Order(book._next_id(), side=Side.SELL, price=10050, qty=50)
book.add(resting)
fills = book.execute(Order(book._next_id(), side=Side.BUY, price=10050, qty=20))
# fills == [(resting_order_id, price, qty), ...]

# view the top of book
bids, asks = book.depth(n=5)
book.printer(bids, asks)
```

To fuzz the book against every invariant (seeded and reproducible):

```bash
python3 python/driver.py   # random seed, printed to stdout
```

```python
from driver import run_driver
run_driver(n_events=20_000, seed=42)  # deterministic: same seed -> same event log
```

To build and test the C++ port:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To replay a full LOBSTER trading day (download a sample first, e.g. from the
`totalorganfailure/lobster-data` Hugging Face mirror, into `data/lobster/`;
these files aren't checked in — see `SPEC.md` §4c):

```bash
./build/tools/lobster_replay data/lobster/AAPL_10/message.csv data/lobster/AAPL_10/orderbook.csv
```

To benchmark (a separate Release build — the default `build/` above is a
debug build, unsuitable for timing):

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
./build-release/tools/benchmark data/lobster/AAPL_10/message.csv
```

## Benchmark results

Phase 5 baseline (naive `std::map<Price, std::deque<Order>>`, no order-ID
index). See `SPEC.md` §7 for full methodology (machine, compiler, warm-up).

| | Throughput | p50 | p99 | p99.9 |
|---|---|---|---|---|
| Overall | ~660-710k msg/s | 125 ns | 7.5-9.9 µs | 14-32 µs |
| New (`add`) | | 42 ns | ~210-250 ns | 1.4-2.9 µs |
| Delete (`cancel`) | | ~2.3 µs | ~9-13 µs | ~16-42 µs |
| PartialCancel (`reduce`) | | ~2.5 µs | ~8-14 µs | ~17-45 µs |
| ExecuteVisible (`reduce`) | | ~3.1 µs | ~12-15 µs | ~28-48 µs |

`add()` is ~50-60x faster than `cancel()`/`reduce()` at p50 — those do a
linear scan across every price level and order to find an order by ID, with
no index yet. That gap is exactly what Phase 6 step 3 (O(1) cancel via a
hash map from order ID to node) targets.

