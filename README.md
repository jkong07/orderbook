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

What's implemented for optimization (phase 6 done — see [Architecture](#architecture-after-phase-6-optimization) and [Benchmark results](#benchmark-results) below):

- `orderbook::index_` — an `unordered_map<OrderId, {side, price, iterator}>` giving `cancel()`/`reduce()`/`execute()` O(1) lookup of a resting order's node, replacing a linear scan across every price level
- `orderbook::PriceLevelArray` (`include/orderbook/price_level_array.hpp`) — a flat, array-indexed replacement for `std::map<Price, OrderList>`, indexed by `price - base_` with a tracked best-occupied cursor, replacing red-black-tree pointer-chasing for level lookup and best-price iteration
- `orderbook::IntrusiveOrderList` (`include/orderbook/intrusive_order_list.hpp`) — orders link into their price level via their own `Order::prev`/`next` fields instead of living inside a container-owned node
- `orderbook::OrderPool` (`include/orderbook/order_pool.hpp`) — bump-allocates `Order` nodes out of 4096-node chunks and recycles released ones via an intrusive free list, replacing per-order `new`/`delete` with O(1) acquire/release off the hot path
- `struct alignas(64) Order` (`order.hpp`) — pads `Order` to one full cache line inside `OrderPool`'s contiguous chunks

## Status

- [x] **Phase 1** — Python prototype: add/cancel/execute logic, terminal depth-view renderer — `add()`, `cancel()`, `execute()`, `depth()`, `printer()`
- [x] **Phase 2** — Invariant checks (`invariants.py`: crossed book, non-positive qty, empty levels, price/side consistency, FIFO ordering, unique IDs, per-side conservation) + seeded randomized event fuzzing (`driver.py`), checked after every event
- [x] **Phase 3** — Port to C++: CMake, GoogleTest, GitHub Actions CI (macOS + Ubuntu), naive baseline with `std::map<Price, std::deque<Order>>`, hand-verified sequences ported to GoogleTest cases
- [x] **Phase 4** — Parse real Nasdaq ITCH 5.0 data via LOBSTER samples, replay it, validate against published snapshots. Free LOBSTER samples turned out to not support exact full-day reconstruction (windowed to trading hours, so pre-existing resting orders are invisible — see `SPEC.md` §4c); validation was scoped to invariants holding over the full real day (0 violations across 400,391 messages) plus exact snapshot matching over a short real window
- [x] **Phase 5** — Benchmark p50/p99/p99.9 latency and throughput — see results below and `SPEC.md` §7
- [x] **Phase 6** — Optimize incrementally: O(1) cancel via a hash-map index → array-indexed price levels → intrusive linked lists → memory pool → cache alignment, benchmarked after each change (run in that order, not the order originally listed — see [Architecture](#architecture-after-phase-6-optimization)). 660-710k → ~21.4-22.0M msg/s, 125ns → 41ns overall p50
- [x] **Phase 7** — Write-up: this README, plus `SPEC.md` §7 for full methodology and per-step commentary

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

## Architecture (after Phase 6 optimization)

The book still exposes the same API it did after the Phase 3 port —
`add()`, `cancel()`, `reduce()`, `execute()`, `depth()` — but every piece of
its internals changed shape in Phase 6, in this order:

1. **O(1) cancel** — `cancel()`/`reduce()`/`execute()` used to find a resting
   order by scanning every price level and every order within it. An
   `unordered_map<OrderId, {side, price, iterator}> index_` now maps an
   order ID directly to its node, so removal is a hash lookup plus a list
   erase. Run first because Phase 5's per-event-type breakdown showed
   `cancel()`/`reduce()` at ~2.3-3.1µs p50 versus `add()`'s 42ns — the scan
   was the dominant bottleneck by a wide margin.
2. **Array-indexed price levels** — `std::map<Price, OrderList>` (red-black
   tree, pointer-chasing on every level lookup and best-price walk) replaced
   by `PriceLevelArray` (`include/orderbook/price_level_array.hpp`): a flat
   `std::vector<OrderList>` indexed by `price - base_`, grown on demand, with
   a tracked best-occupied cursor instead of tree iteration.
3. **Intrusive linked lists** — `std::list<Order>` per price level replaced
   by `IntrusiveOrderList` (`include/orderbook/intrusive_order_list.hpp`):
   orders link via their own `Order::prev`/`next` fields, each individually
   heap-allocated via plain `new`/`delete` instead of living in a
   container-owned node. **This step alone was a measured regression** (see
   results below) — `std::list`'s allocator was already doing one
   allocation per node, so removing the container wrapper bought nothing
   while exposing raw `new`/`delete` directly on the hot path.
4. **Memory pool** — `OrderPool` (`include/orderbook/order_pool.hpp`):
   bump-allocates `Order` nodes out of 4096-node chunks
   (`std::unique_ptr<Order[]>`) and recycles released ones through an
   intrusive free list that reuses `Order::next` as the link. Replaces step
   3's per-order `new`/`delete` with O(1) pointer-arithmetic acquire/release,
   recovering step 3's regression and then some — the best and most stable
   result in the table.
5. **Cache alignment** — `struct alignas(64) Order` pads `Order` from 48 to
   64 bytes so no order straddles two cache lines inside `OrderPool`'s
   contiguous chunks. Flat-to-slightly-worse here (see
   [Limitations](#limitations-honest-results-from-phase-6)) — kept in the
   codebase and reported as-is rather than reverted.

Every step was run and benchmarked in isolation — never two performance
changes bundled into one commit — and re-verified against the full test
suite (38/38, including a `-fsanitize=address,undefined` debug build) plus
the full-day LOBSTER invariant replay (0 violations) before moving to the
next step. Full per-step rationale and settled-decision log: `SPEC.md` §4b,
§7.

## Benchmark results

Same fixed input, warm-up, machine, and build config for every row (LOBSTER
AAPL 2012-06-21 full trading day, 400,391 messages, first 10,000 discarded
as warm-up, Release build on Apple Silicon/Apple Clang 17). Full methodology:
`SPEC.md` §7.

| Step | Change | Throughput | Overall p50 | Overall p99 | Overall p99.9 |
|---|---|---|---|---|---|
| 0 (baseline) | Naive `std::map`, linear order-ID scan | ~660-710k msg/s | 125 ns | 7.5-9.9 µs | 14-32 µs |
| 1 (O(1) cancel) | `unordered_map` index into per-level lists | ~15.5-16.1M msg/s | 42 ns | 125 ns | 375-416 ns |
| 2 (array-indexed levels) | `PriceLevelArray` replaces `std::map` | ~21.3-22.1M msg/s | 41 ns | 84 ns | 250-500 ns |
| 3 (intrusive lists) | `IntrusiveOrderList`, per-order `new`/`delete` | ~19.5-20.7M msg/s ⚠️ | 41 ns | 84 ns | 333-792 ns |
| 4 (memory pool) | `OrderPool` bump allocator + free list | ~22.1-22.8M msg/s | 41 ns | 83-84 ns | 167-209 ns |
| 5 (cache alignment) | `alignas(64) Order` | ~21.4-22.0M msg/s | 41 ns | 84 ns | 167 ns |

Baseline → final: **~660-710k → ~21.4-22.0M msg/s throughput (≈30x), 125ns →
41ns overall p50 (≈3x)**, with p99.9 tail latency down roughly two orders of
magnitude (14-32µs → 167ns).

Baseline per-event-type breakdown (what motivated running step 1 first):

| Event type | p50 | p99 | p99.9 |
|---|---|---|---|
| New (`add`) | 42 ns | ~210-250 ns | 1.4-2.9 µs |
| Delete (`cancel`) | ~2.3 µs | ~9-13 µs | ~16-42 µs |
| PartialCancel (`reduce`) | ~2.5 µs | ~8-14 µs | ~17-45 µs |
| ExecuteVisible (`reduce`) | ~3.1 µs | ~12-15 µs | ~28-48 µs |

`add()` was ~50-60x faster than `cancel()`/`reduce()` at baseline p50 —
`add()` only pays for a map insert near the top of book, while
`cancel()`/`reduce()` linearly scanned every price level and order to find
one by ID. After step 1, `cancel()`/`reduce()` p50 dropped to the timer's
noise floor (0-42ns) and tail latencies collapsed 20-100x across the board.

### Limitations (honest results from Phase 6)

Not every step was a win, and both are reported as measured rather than
reframed or dropped:

- **Step 3 (intrusive lists) is a real regression on its own**
  (~21.3-22.1M → ~19.5-20.7M msg/s, confirmed stable across 5+ runs, not
  noise). `std::list` was already allocating one node per order; trading its
  allocator for raw `new`/`delete` removed a layer of indirection without
  removing the allocation itself, and exposed it directly on the hot path.
  Kept as its own table row rather than folded into step 4's number, so
  step 4's win doesn't mask step 3's loss.
- **Step 5 (cache alignment) is flat-to-slightly-worse**
  (~22.1-22.8M → ~21.4-22.0M msg/s, stable across 8 runs). Two reasons: (1)
  "reduce false sharing" — half the technique's usual payoff — has no target
  in a single-threaded matching engine with no concurrent access to `Order`
  from different cores. (2) `Order` was already 48 bytes, mostly fitting in
  one cache line; padding to 64 is a 33% larger footprint for every resting
  order, and this workload's 1-2-orders-touched-per-message access pattern
  doesn't do enough sequential scanning to recoup that cost. The technique
  isn't wrong in general — it would plausibly pay off in a multi-threaded
  variant of this book with concurrent market-data fan-out across cores —
  it just doesn't match this project's access pattern. Left in the codebase
  rather than reverted, since the point of Phase 6 was measuring each
  candidate honestly, not cherry-picking wins.
- **LOBSTER's free samples can't validate a full day exactly** — windowed to
  trading hours, so pre-existing resting orders from before 09:30 are
  invisible to the replay and the book's reconstruction structurally drifts
  from the published snapshot within the first few hundred messages
  (0.53% of rows have a correct best bid/ask over a full day). This isn't an
  `OrderBook` bug — full-day validation instead runs the ported invariants
  (positive qty, unique IDs, per-side conservation), which hold regardless
  of untracked pre-existing orders. Exact snapshot-diff validation is scoped
  to a short real window instead. See `SPEC.md` §4c for the full
  investigation.
- **Single-threaded only** — no concurrent order submission, no lock-free
  structures, no multi-core market-data fan-out. This is the natural next
  axis of optimization and the one step 5's cache alignment is actually
  built for; it's out of scope for this project as currently framed.
- **No differential testing against the Python prototype** — considered and
  deliberately cut (`SPEC.md` §6). Hand-verified GoogleTest cases plus
  real-data validation were judged sufficient for this project's goals.

