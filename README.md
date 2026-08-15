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

Everything else (ITCH parsing, benchmarks) is not yet built — see Status below.

## Status

- [x] **Phase 1** — Python prototype: add/cancel/execute logic, terminal depth-view renderer — `add()`, `cancel()`, `execute()`, `depth()`, `printer()`
- [x] **Phase 2** — Invariant checks (`invariants.py`: crossed book, non-positive qty, empty levels, price/side consistency, FIFO ordering, unique IDs, per-side conservation) + seeded randomized event fuzzing (`driver.py`), checked after every event
- [x] **Phase 3** — Port to C++: CMake, GoogleTest, GitHub Actions CI (macOS + Ubuntu), naive baseline with `std::map<Price, std::deque<Order>>`, hand-verified sequences ported to GoogleTest cases
- [ ] **Phase 4** — Parse real Nasdaq ITCH 5.0 data via LOBSTER samples, replay it, validate against published snapshots
- [ ] **Phase 5** — Benchmark p50/p99/p99.9 latency and throughput
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

## Roadmap

See the phase checklist under [Status](#status) above.
