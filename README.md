# orderbook

A low-latency limit order book and market data feed handler, prototyped in Python and being ported to C++.

## Why I built this

My other projects (a MATH-benchmark LLM prompting eval, an options pricing engine) are numerical and scripting-heavy — they don't touch the systems-engineering side of quant/trading infrastructure. This project is meant to close that gap: memory layout, cache behavior, lock-free structures, latency measurement, and binary protocol parsing, built and benchmarked incrementally rather than assumed.

## Features

What's actually implemented so far (Python prototype, phase 1, in progress):

- `Order` class — `order_id`, `side` (`Side` enum: `BUY`/`SELL`), `price` (integer cents), `qty`
- `OrderBook` class — bids and asks held as `SortedDict[price -> deque[Order]]`
- `add()` — inserts a passive (non-crossing) order at a given price level

Everything else (cancel, execute, depth-view renderer, invariant checks, C++ port, ITCH parsing, benchmarks) is not yet built — see Status below.

## Status

- [ ] **Phase 1** — Python prototype: add/cancel/execute logic, terminal depth-view renderer, scripted event sequences as test cases *(in progress — `add()` only so far)*
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
from book import OrderBook, Side

book = OrderBook()
book.add(Side.BUY, price=10050, qty=100)
```

## Roadmap

See the phase checklist under [Status](#status) above.
