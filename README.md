# orderbook

A low-latency limit order book and market data feed handler, prototyped in Python and being ported to C++.

## Why I built this

I like working at the level where memory layout and cache behavior actually decide performance instead of being an afterthought. An order book is a good excuse to build that muscle: start naive, measure everything, and optimize one change at a time.

## Features

What's actually implemented so far (Python prototype, phase 1, in progress):

- `Order` class — `order_id`, `side` (`Side` enum: `BUY`/`SELL`), `price` (integer cents), `qty`
- `OrderBook` class — bids and asks held as `SortedDict[price -> deque[Order]]`
- `add()` — inserts a passive (non-crossing) order at a given price level
- `cancel()` — removes a resting order by `order_id`, pruning the price level if it empties out
- `execute()` — matches an incoming order against resting orders on the opposite side (price-time priority), reducing/removing resting orders as it fills
- `depth(n=None)` — aggregates resting qty per price level for the top `n` bid/ask levels (all levels if `n` is omitted), returned as `(bid_levels, ask_levels)` price/qty tuples
- `printer(bid_list, ask_list)` — renders a `depth()` result as a two-column terminal table, asks above bids, best prices innermost

Everything else (invariant checks, C++ port, ITCH parsing, benchmarks) is not yet built — see Status below.

## Status

- [ ] **Phase 1** — Python prototype: add/cancel/execute logic, terminal depth-view renderer, scripted event sequences as test cases *(in progress — `add()`, `cancel()`, `execute()`, `depth()`, `printer()` so far)*
- [ ] **Phase 2** — Invariant checks (spread never crosses, no negative/zero-qty orders, price-level sums match order sums, conservation of shares) + randomized event fuzzing
- [ ] **Phase 3** — Port to C++: CMake, Google Test, GitHub Actions CI from day one, starting naive with `std::map<Price, std::deque<Order>>`
- [ ] **Phase 4** — Parse real Nasdaq ITCH 5.0 data via LOBSTER samples, replay it, validate against published snapshots
- [ ] **Phase 5** — Benchmark p50/p99/p99.9 latency and throughput
- [ ] **Phase 6** — Optimize incrementally (array-indexed price levels → intrusive linked lists → O(1) cancel via hash map → memory pool → cache alignment), benchmarking after each change and recording results in a table

## How to run it

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install sortedcontainers
```

Then, from a REPL:

```python
from book import Order, OrderBook, Side

book = OrderBook()
order_id = book.add(Order(order_id=None, side=Side.BUY, price=10050, qty=100))
book.cancel(order_id)

# match an incoming order against the opposite side of the book
book.execute(Order(order_id=None, side=Side.SELL, price=10050, qty=50))

# view the top of book
bids, asks = book.depth(n=5)
book.printer(bids, asks)
```

## Roadmap

See the phase checklist under [Status](#status) above.
