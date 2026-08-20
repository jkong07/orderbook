# Limit Order Book & Market-Data Feed Handler — Project Spec

**Repo:** `orderbook` (public, GitHub)
**Languages:** Python (prototype, complete) → C++20 (production implementation)
**Status as of 2026-08-14:** Phases 1–3 complete.

---

## 1. Purpose

Build a low-latency limit order book with a market-data feed handler, in C++, as a
portfolio project for FAANG SWE recruiting and as groundwork for quant-trading
recruiting later.

The project is chosen because it sits at the intersection of three things that are
hard to fake: non-trivial data structure design, measurable performance work with
before/after numbers, and validation against real exchange data. The final artifact
should be a repo where a reader can see correctness tests, a benchmark table showing
each optimization's effect, and evidence that the book matches published real-world
snapshots.

### Success criteria

- Correct against hand-verified sequences and invariant checks
- Correct against real Nasdaq ITCH data, validated against published LOBSTER snapshots
- A README table showing p50 / p99 / p99.9 latency and throughput across successive optimizations
- CI green on macOS and Linux from the first commit onward

---

## 2. Working constraints

These govern how the project is built, not just what it is.

- **No skipping the unglamorous phases.** Real-data validation and correctness tests
  come before optimization, always. A fast book that is subtly wrong is worthless, and
  a benchmark without a correct baseline is not a result.
- **One optimization at a time, benchmarked.** Never bundle two performance changes.
  The entire value of the benchmark table is attribution.
- **Design decisions get written down** in `CLAUDE.md` / this spec as they are settled,
  so they are not re-litigated.

---

## 3. Phase roadmap

### Phase 1 — Python prototype ✅ COMPLETE

Plain data structures, correctness over speed. Purpose: understand the domain and
produce a reference implementation to validate the C++ port against.

- `Order` as a dataclass; `OrderBook` holding bids/asks as `SortedDict`
- `add()` for passive orders, `cancel(order_id)`, `execute(order)` for aggressive orders
- Terminal depth-view renderer
- Verified against hand-predicted event sequences

### Phase 2 — Invariants and randomized testing ✅ COMPLETE

- State invariants (no empty price levels, ordering holds, no crossed book)
- Conservation invariant (quantity in = quantity resting + quantity filled)
- Seeded random event generator and driver

### Phase 3 — C++ port ✅ COMPLETE

Deliberately the naive implementation: `std::map<Price, std::deque<Order>>`. This is the
correctness baseline and the performance baseline. It is not meant to be fast.

- ✅ CMake project (C++20, `src/` + `include/` + `tests/`, `-Wall -Wextra -Wpedantic`)
- ✅ GoogleTest via `FetchContent`, pinned commit
- ✅ GitHub Actions CI building and testing on macOS and Ubuntu
- ✅ Type and API decisions settled (see §4b)
- ✅ Hand-verified sequences ported to GoogleTest cases — *written before the implementation*
- ✅ `Order` → empty book → `add` → `cancel` → `execute` → depth, in that order

### Phase 4 — Real market data ✅

- Parse Nasdaq ITCH 5.0 via LOBSTER sample files
- Handle the message types needed to reconstruct the book (add, cancel, delete,
  execute, replace)
- Validate reconstructed book state against LOBSTER's published snapshots

Done, with a scope adjustment discovered along the way — see §4c. Free LOBSTER
samples can't support exact full-day snapshot reconstruction (a data-source
limitation, not an `OrderBook` bug), so exact-match validation is scoped to a
short real window (`tests/lobster_replay_test.cpp`) and full-day validation
runs the ported invariants instead: **0 violations across all 400,391 real
messages** in the AAPL 2012-06-21 sample (`tools/lobster_replay`, run
manually — see §4b for why it isn't in `ctest`).

This phase is where most naive implementations break. Real feeds contain message
sequences that synthetic tests never generate.

### Phase 5 — Benchmark baseline ✅ *(resume-ready checkpoint)*

- Measure p50 / p99 / p99.9 per-message latency and overall throughput
- Establish measurement methodology before optimizing (see §7)
- README table with the baseline row filled in

Done — see §7 for the full methodology and results. Baseline:
~660–710k msg/s, 125ns overall p50. `add()` runs ~50-60x faster than
`cancel()`/`reduce()` at p50 (42ns vs. ~2.3-3.1µs) because cancel/reduce do
a linear order-ID scan with no index — the clear, expected target for
Phase 6 step 3 (O(1) cancel via hash map).

At this point the project is presentable even if nothing further is done.

### Phase 6 — Optimization ✅ *(all 5 steps done, run out of original order)*

Originally listed in the order below; step 3 (O(1) cancel) was promoted to
run first because the Phase 5 baseline breakdown (§7) showed it was the
dominant bottleneck by a wide margin (`cancel()`/`reduce()` 50-60x slower
than `add()` at p50), then step 1 (array-indexed price levels), then step 2
(intrusive linked lists), then step 4 (memory pool), then step 5 (cache
alignment) — benchmarking after each and adding a row to the table:

1. ✅ **Array-indexed price levels** — `PriceLevelArray<Descending>` (flat
   `std::vector<OrderList>` indexed by price offset, tracked best cursor)
   replaces `std::map`. Removes tree traversal and pointer chasing from level
   lookup. See §7 (~15.5–16.1M → ~21.3–22.1M msg/s, overall p99 125ns → 84ns).
2. ✅ **Intrusive linked lists** — `IntrusiveOrderList`; orders carry their own
   `prev`/`next` pointers rather than living inside a `std::list`-owned node,
   each resting order individually heap-allocated. A small, honest *regression*
   (~21.3–22.1M → ~19.5–20.7M msg/s) — see §7 for why (raw `new`/`delete` per
   order, no pooling yet) and how step 4 recovered it.
3. ✅ **O(1) cancel** — `unordered_map<OrderId, {side, price, iterator}>` index into
   per-level `std::list<Order>` (swapped from `std::deque` for iterator stability), so
   `cancel()`/`reduce()`/`execute()`'s resting-order removal are no longer a scan. See §7
   for results (~660–710k → ~15.5–16.1M msg/s, 125ns → 42ns overall p50).
4. ✅ **Memory pool** — `OrderPool`; 4096-node chunks with an intrusive free list
   (reusing `Order::next`), replacing step 2/3's per-order `new`/`delete`. Recovered
   step 3's regression and then some (~19.5–20.7M → ~22.1–22.8M msg/s — best throughput
   *and* tightest tail-latency variance of any step yet). See §7.
5. ✅ **Cache alignment** — `alignas(64) Order`, so no order straddles two cache
   lines in `OrderPool`'s contiguous chunks. Flat-to-slightly-worse in this
   single-threaded workload (~22.1–22.8M → ~21.4–22.0M msg/s) — see §7 for why
   (no false sharing to reduce without threads; the 33% larger footprint costs
   more than the occasional-straddle savings here). Reported as-is, not reverted.

### Phase 7 — Write-up ✅

README covering architecture, correctness strategy, benchmark methodology, results
table, and an honest limitations section.

---

## 4. Settled design decisions (Python prototype)

| Decision | Rationale |
|---|---|
| Prices stored as `int` cents, never float | Float keys produce mismatched price levels through rounding |
| `qty` tracks *remaining* quantity | Single source of truth; avoids original-vs-filled bookkeeping |
| Caller constructs the `Order` and mints the ID | `submit()` was planned and dropped; `add()` no longer generates IDs |
| `add(order)` and `execute(order)` both take an `Order` object | Uniform API |
| `execute()` mutates the incoming order's `qty` in place | Caller can see what remains unfilled — *under review for C++, see §5* |
| `execute()` returns fills; `cancel()` returns the removed order | Fill list is not deferred |
| Empty price levels are deleted, not left in place | Keeps the invariant that every existing level is non-empty |
| Depth view split in two | One function returns aggregated per-level data (takes `int n` = levels deep, default all), a separate one formats and prints |
| Default branch renamed `master` → `main` | Convention |
| Task tracking in a private Kanban tool, not GitHub Projects | Public-visibility concerns |

---

## 4b. Settled design decisions (C++ port)

| Decision | Rationale |
|---|---|
| `Price` is `int64_t` cents, via `using Price = std::int64_t;` (plain alias, not a wrapper type) | Signed avoids unsigned-underflow-wraps-to-huge-positive on intermediate computations (spread/distance from best), even though a resting order's price is never itself negative; plain alias chosen over a wrapper struct for zero overhead and simplicity — revisit if `Price`/`Qty`/`OrderId` mixups become a real bug source. Assert positivity at construction time (e.g. `Order`'s constructor) rather than relying on the type. |
| `Qty` is `int64_t`, via `using Qty = std::int64_t;` (same alias pattern as `Price`) | Same underflow argument as `Price`: `execute()` naturally does `resting.qty -= fill_qty`, and a partial-fill/overfill bug should go visibly negative (catchable by assertion) rather than silently wrap to a huge positive with unsigned. Assert positivity/non-negativity at the relevant boundaries rather than relying on the type. |
| `Side` is `enum class Side : std::uint8_t { Buy, Sell };` | `enum class` (not plain `enum`) avoids leaking `Buy`/`Sell` into the enclosing scope and blocks implicit int conversion. `uint8_t` underlying type shrinks it from the default 4 bytes to 1 — cheap to set now, and relevant later since `Side` lives inside `Order` and struct size/padding affects how many orders fit in cache (Phase 6 territory), so setting it correctly now avoids a retrofit once `Order`'s layout is baked in. |
| Bid ordering: bids use `std::map<Price, std::deque<Order>, std::greater<Price>>`, asks use plain `std::map<Price, std::deque<Order>>` — bids and asks are different types | Puts the best-first-ordering asymmetry (bids: highest price first; asks: lowest price first) into the type system rather than into every call site. Shared logic (`execute()`, `depth()`, invariant checkers) must be templated over the comparator, but that's a one-time cost; the alternative (same type both sides, manual reverse-iteration on bids) risks a silent bug at any future call site that forgets bids iterate backwards — `begin()` on a bid book would compile fine while walking worst-price-first. Chosen for consistency with the spec's correctness-first bias (§2, §6). |
| Ownership: `add(Order order)` takes by value and moves the order into the level's `deque<Order>`; the book owns resting orders outright, no external aliasing | Resting orders live by value directly in the `deque`, not behind a pointer — avoids pointer-chasing on every level walk (would fight Phase 6's cache-alignment goals). Caller's local `Order` is moved-from after `add()` and shouldn't be touched again — a deliberate behavior change from the Python prototype, where the caller keeps a live, mutation-visible reference to the same object via Python's reference semantics. Nothing outside the book needs to observe a resting order's mutations once it's resting, so no lifetime-sharing mechanism (`unique_ptr`, `shared_ptr`) is needed. |
| `cancel()` returns `std::optional<Order>` — present (moved out of the `deque`, not copied) if found and removed, `std::nullopt` if not found | Direct translation of Python's "the order, or `None`". No exception for the not-found case, consistent with treating "order doesn't exist" as an expected outcome rather than an error. |
| `struct Fill { OrderId resting_id; Price price; Qty qty; };` — fills and remaining quantity returned together as `struct ExecuteResult { std::vector<Fill> fills; Qty remaining_qty; };` from `execute()`, by value | Replaces the Python prototype's in-place mutation of the incoming order's `qty` (§4, row: `execute()` mutates in place). A non-const reference parameter would've made that mutation invisible at the call site (`book.execute(order);` gives no visual sign `order` changed) — the exact "corruption that produces plausible-looking output" class of bug §6 is trying to catch. `execute()` instead takes the incoming order by value/const reference (unmutated) and returns `ExecuteResult` — `auto result = book.execute(order);` makes both fills and what's left unfilled visible at the call site, no hidden state to remember. |
| `OrderBook::reduce(OrderId, Qty)` added alongside `cancel()`/`execute()`, returns `std::optional<Order>` (post-reduction state, or the zeroed order right before removal) | Needed for LOBSTER replay (Phase 4): a resting order shrinking via partial-cancel or a resting-side execution must keep its queue position, unlike cancel-and-re-add. Mirrors `cancel()`'s not-found convention (`nullopt`, no exception). |
| `include/orderbook/lobster.hpp` + `lobster.cpp`: `parse_message_line()`, `for_each_message()`, `apply(OrderBook&, Message)` → `ApplyEffect` | Straight translation of LOBSTER's 6-column message format (see §4c) into calls on the existing `OrderBook` API — `New`→`add()`, `PartialCancel`/`ExecuteVisible`→`reduce()`, `Delete`→`cancel()`, `ExecuteHidden`/`Cross`/`Halt`→ no-op. `ApplyEffect` reports actual qty moved (0 if the targeted order wasn't found) so callers can tally conservation without re-deriving it. |
| `include/orderbook/lobster_validate.hpp` + `lobster_validate.cpp`: `parse_orderbook_line()`, `compare_to_snapshot()`, `seed_from_snapshot()`, `snapshot_before()` | Split from `lobster.hpp` because it's validation/diagnostic machinery, not book-driving logic — a consumer that only wants to replay a message stream (e.g. a future benchmark harness) shouldn't need to link snapshot-parsing code. `seed_from_snapshot()`/`snapshot_before()` exist specifically to work around the windowed-sample limitation in §4c. |
| `include/orderbook/invariants.hpp` + `invariants.cpp`, ported from `python/invariants.py` | Only `check_not_crossed`, `check_positive_qty`, `check_unique_ids`, `check_conservation` were ported — `check_no_empty_levels`, `check_price_side_consistency`, `check_fifo_order` are structurally guaranteed by `OrderBook`'s own implementation (map key always equals resting price, levels are pruned on empty, orders are never reordered within a `deque`) and can't be violated from outside the class, so porting them would test the language's memory model, not this project's logic. `OrderBook::bids()`/`asks()` were added as const accessors so invariants.cpp (and other external code) can walk resting orders — `depth()` only exposes aggregated per-level data, not individual orders. |
| `check_not_crossed` excluded from LOBSTER-replay's invariant set (still used as-is for synthetic/`execute()`-driven tests) | LOBSTER logs an "Add" and its matching "Execute" message(s) as *separate* rows (§4c) — a marketable order legitimately locks or crosses the book for exactly one message before the next row's Execute consumes it. That's expected behavior at LOBSTER's message granularity, not a bug; asserting "never crossed" after every single LOBSTER message would fail on correct replay. |
| `Order` gained public `prev`/`next` (`Order*`, default `nullptr`) — Phase 6 step 2 (intrusive linked lists) | Needed so `Order` nodes can link into `IntrusiveOrderList` without a separate container-owned wrapper node (`include/orderbook/intrusive_order_list.hpp`). Deliberately on the public struct rather than a private book-internal wrapper type, since keeping one `Order` type (not `Order` + `OrderNode : Order`) avoids upcast/downcast noise at every call site that already passes `Order` around (`add()`, fills, invariants, LOBSTER replay). The tradeoff: these fields are meaningless outside `OrderBook`, so every `Order` handed back to a caller (`cancel()`/`reduce()`'s returned `Order`) has them explicitly zeroed before return — callers should never read them, but they're publicly visible, which is worth knowing before adding new code that constructs or copies `Order` values. |
| Resting orders now individually heap-allocated instead of living by value inside a container node — Phase 6 step 2, allocation strategy replaced by `OrderPool` in step 4 | Required by the intrusive-list design above: `Order::prev`/`next` are raw pointers to other `Order`s, so a resting order needs a stable address for the lifetime it's linked, which `std::vector`-style relocation can't provide. This is a real ownership-model change from the original §4b row ("book owns resting orders outright... no external aliasing") — still true (no *external* aliasing), but "owns" now means "owns a slot in `pool_`," not "owns a value." Copy-disabled (`OrderBook(const OrderBook&) = delete`), since a shallow copy would let both books' nodes alias the same pool — move is still fine (nothing points back at the `OrderBook` itself). Verified leak/UB-free via a `-fsanitize=address,undefined` debug build across the full test suite at both the plain-`new`/`delete` (step 2) and pooled (step 4) stages. |
| `OrderPool` (`include/orderbook/order_pool.hpp`) — bump allocator over `std::unique_ptr<Order[]>` chunks (4096 nodes each) with an intrusive free list reusing `Order::next` as the link — Phase 6 step 4 | Replaces step 2's per-order `new`/`delete`, which measured as a real regression (§7) versus step 1's array-only baseline — allocator variance (lock contention, free-list search, page faults on chunk growth) was showing up directly in tail latency. A bump/free-list pool turns acquire/release into pointer arithmetic off the hot path; chunk growth is the only remaining `unique_ptr`-driven allocation, and it's rare (one per 4096 orders — under 50 for the full AAPL trading day used in benchmarking). Simplified `OrderBook` too: since the pool's chunks own all node memory, `~OrderBook()`'s manual per-order-`delete` walk (added in step 2) was removed in favor of `= default` — chunk teardown frees everything. Pool doesn't track which nodes are live (`OrderBook` still does, via `index_` and the per-level lists) — same "container manages links/slots, not lifetime" split as `IntrusiveOrderList` not owning memory either. |
| `Order` given `alignas(64)`, growing it from 48 to 64 bytes — Phase 6 step 5, kept despite measuring flat-to-slightly-worse | Guarantees every order occupies exactly one cache line inside `OrderPool`'s contiguous chunks rather than straddling two at some array offsets. Measured ~22.1-22.8M → ~21.4-22.0M msg/s (§7) — the larger per-order footprint costs more in this single-threaded, few-orders-per-message workload than the straddle-avoidance saves, and "reduce false sharing" (the step's other stated goal) has no target without concurrent access to `Order`. Left in rather than reverted: Phase 6's purpose was measuring each candidate honestly against the baseline, not cherry-picking wins, and the technique would plausibly pay off in a multi-threaded fan-out variant of this book — noted as a limitation, not silently dropped. |

---

## 4c. Phase 4 finding: LOBSTER's windowed samples can't be exactly reconstructed

Discovered while building the replay/validation tooling above, against the free
LOBSTER sample (`AAPL_2012-06-21`, 10 levels, downloaded from the
`totalorganfailure/lobster-data` Hugging Face mirror — `lobsterdata.com` itself
now gates samples behind a registration/terms-acceptance portal).

**The finding:** LOBSTER's free samples are windowed to regular trading hours
(09:30–16:00). The message file only contains events from within that window,
but the order book at 09:30:00 already has resting orders from before it
(pre-market / the opening auction) — confirmed by seeding a replay with the
true pre-window state (backed out of row 1 of the orderbook file) and finding
it matches exactly at row 0. That state's individual orders — their real
order IDs and the breakdown of how much resting quantity at a price is one
order vs. several — are fundamentally unrecoverable from the sample: the
snapshot file only ever reports each price level's *aggregate* size, never
per-order detail, and the message file never emitted a "New" event for any
order that predates the window's start. So even the deep book beneath the
visible top 10 levels at 09:30:00 is invisible to us.

The consequence isn't a brief, self-correcting startup glitch — it's
structural and permanent. As soon as a message later in the day
cancels/executes one of those untracked pre-existing orders (confirmed at
message 12 for the seeded top-10 orders, and again at message ~440 for a
deeper untracked order that later surfaced), our reconstruction can't apply
that removal (it never had the order to remove), so the corresponding book
level silently drifts from ground truth from that point on, for the rest of
the day. Measured over the full AAPL trading day: **0.53% of rows have a
correct best bid/ask**, dropping further for deeper levels. This is not an
`OrderBook` correctness bug — the same drift occurs regardless of
implementation quality, since it's a property of the input data, not the
matching logic applied to it.

**Resulting validation strategy** (see §6): exact snapshot-diff validation
(`lobster_validate.hpp`'s `compare_to_snapshot()`) is retained but scoped to
the short real window before this drift begins — proves message parsing,
event-type dispatch, and price/qty conversion are correct against real
exchange data (`tests/lobster_replay_test.cpp`, `tests/fixtures/`). Full-day
validation instead runs the ported invariants (`check_positive_qty`,
`check_unique_ids`, `check_conservation`) — these hold regardless of
untracked pre-existing orders, because they're derived from `OrderBook`'s own
tracked state rather than compared against ground truth
(`tools/lobster_replay.cpp`, run manually against the full downloaded
sample — not part of `ctest`, see §4b).

Chasing a data source without this limitation (a full, unwindowed
NASDAQ TotalView-ITCH feed starting from midnight) was considered and
rejected: it requires a paid LOBSTER/NASDAQ subscription, which is out of
scope for this project's goals.

---

## 5. Open decisions for the C++ port

All items settled — see §4b. This section is kept as a placeholder in case new
decisions surface once book logic is actually being written.

---

## 6. Correctness strategy

Three independent layers, each catching what the others miss:

1. **Hand-predicted sequences** — outcomes worked out on paper before running. Covers
   both sides, limit-stopped walks, zero-fill, book exhaustion, cancel-after-partial-fill,
   same-price ordering, cancel from the middle of a level, cancel emptying a level.
   Catches misunderstandings of the domain.
2. **Invariants** — state invariants and the conservation invariant, checked after every
   operation in test builds. Catches corruption that produces plausible-looking output.
3. **Real-data validation** — LOBSTER published snapshots. Catches domain assumptions
   that synthetic data never violates. In practice this caught something one level up
   from "is the matching logic right": the free LOBSTER sample itself can't support
   exact full-day reconstruction (see §4c) — exact-match validation is scoped to a
   short real window, full-day validation runs invariants instead.

Differential testing against the Python prototype (seeded event stream replayed
through both, fill lists diffed) was considered and deliberately cut — the
hand-verified GoogleTest cases plus real-data validation were judged sufficient
coverage for this project's goals, and a Python/C++ differential harness was
extra infrastructure the project didn't need to build to hit its success
criteria.

Every layer runs in CI on both platforms.

---

## 7. Benchmark methodology

Decide before Phase 5 and hold it constant, or the table means nothing.

- Fixed input: the same ITCH sample file for every run
- Report percentiles, not means — tail latency is the point in this domain
- Warm up before measuring; discard the first N messages
- Report the machine, compiler, and flags alongside the numbers
- Same build configuration for every row of the table
- Re-run the full correctness suite at each optimization step — behavior must be identical

**Fixed for every row below:**

| | |
|---|---|
| Input | `data/lobster/AAPL_10/message.csv` (LOBSTER AAPL 2012-06-21, full trading day, 400,391 messages — same file Phase 4 validated against) |
| Warm-up | First 10,000 messages applied untimed, discarded from all statistics |
| Machine | MacBook, macOS 15.7.7, Apple Silicon (arm64) |
| Compiler | Apple Clang 17.0.0 (clang-1700.0.13.5) |
| Build | `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release` (a separate build dir from the default debug `build/`, so day-to-day `ctest` runs never accidentally pick up Release flags or vice versa) |
| Harness | `tools/benchmark.cpp` — pre-parses the whole file into memory first (isolates timed loop from file I/O/CSV parsing), times each `lobster::apply()` call individually with `std::chrono::steady_clock`, reports overall latency plus a breakdown by LOBSTER event type |

**Results:**

| Step | Change | Throughput | Overall p50 | Overall p99 | Overall p99.9 |
|---|---|---|---|---|---|
| 0 (baseline) | Naive `std::map<Price, std::deque<Order>>`, linear order-ID scan for cancel/reduce | ~660–710k msg/s | 125 ns | 7.5–9.9 µs | 14–32 µs |
| 1 (O(1) cancel) | `std::deque<Order>` → `std::list<Order>` per price level (stable iterators) + `unordered_map<OrderId, {side, price, iterator}> index_`; `cancel()`/`reduce()`/`execute()`'s resting-order removal now look up the index instead of scanning every level/order | ~15.5–16.1M msg/s | 42 ns | 125 ns | 375–416 ns |
| 2 (array-indexed price levels) | `std::map<Price, OrderList>` → `PriceLevelArray<Descending>` (`include/orderbook/price_level_array.hpp`): flat `std::vector<OrderList>` indexed by `price - base_`, grown on demand, with a tracked best-occupied cursor replacing tree-ordered iteration | ~21.3–22.1M msg/s | 41 ns | 84 ns | 250–500 ns |
| 3 (intrusive linked lists) | `std::list<Order>` → `IntrusiveOrderList` (`include/orderbook/intrusive_order_list.hpp`): orders link via their own `Order::prev`/`next`, each resting order individually heap-allocated (plain `new`/`delete`) instead of living inside a container-owned node | ~19.5–20.7M msg/s | 41 ns | 84 ns | 333–792 ns |
| 4 (memory pool) | `OrderPool` (`include/orderbook/order_pool.hpp`): bump-allocates `Order` nodes out of 4096-node chunks, recycles released ones via an intrusive free list (reusing `Order::next`) — replaces step 3's per-order `new`/`delete` with O(1) acquire/release off the hot path | ~22.1–22.8M msg/s | 41 ns | 83–84 ns | 167–209 ns |
| 5 (cache alignment) | `struct alignas(64) Order` (`order.hpp`): pads `Order` from 48 to 64 bytes so every order occupies exactly one cache line in `OrderPool`'s contiguous chunks, instead of straddling two | ~21.4–22.0M msg/s | 41 ns | 84 ns | 167 ns |

Per-event-type breakdown at baseline (this is what motivates Phase 6's optimization order):

| Event type | p50 | p99 | p99.9 |
|---|---|---|---|
| New (`add`) | 42 ns | ~210–250 ns | 1.4–2.9 µs |
| PartialCancel (`reduce`) | ~2.5 µs | ~8–14 µs | ~17–45 µs |
| Delete (`cancel`) | ~2.3 µs | ~9–13 µs | ~16–42 µs |
| ExecuteVisible (`reduce`) | ~3.1 µs | ~12–15 µs | ~28–48 µs |

`add()` is ~50-60x faster than `cancel()`/`reduce()` at p50: `add()` only pays for a `map` insert at a price that's usually already near the top of book, while `cancel()`/`reduce()` do a linear scan across every price level and every order within it to find an order by ID — there's no order-ID index yet. This is exactly the gap Phase 6 step 3 ("O(1) cancel — hash map from order ID directly to the order's node") targets, and this baseline is the number that optimization needs to beat.

Per-event-type breakdown after step 1 (O(1) cancel):

| Event type | p50 | p99 | p99.9 |
|---|---|---|---|
| New (`add`) | 42 ns | ~166–167 ns | 542–583 ns |
| PartialCancel (`reduce`) | 0 ns | ~42 ns | 42–84 ns |
| Delete (`cancel`) | 42 ns | ~84 ns | 166–167 ns |
| ExecuteVisible (`reduce`) | 42 ns | ~125 ns | ~125–167 ns |

`cancel()`/`reduce()` p50 drops from ~2.3–3.1 µs to the noise floor of the timer (0–42 ns) — the linear scan is gone, replaced by an `unordered_map` lookup plus an O(1) list erase. `add()` is essentially unchanged, as expected: step 1 targeted the scan, not level lookup (that's array-indexed price levels, still step 1 in SPEC.md's originally-listed order, deferred). Tail latencies (p99/p99.9) across all event types collapsed by roughly 20-100x too, since the old worst case (scanning deep into a large, stale price level) no longer exists.

Full test suite (`ctest --test-dir build`, including LOBSTER snapshot-diff replay) still passes at 38/38 after this change — see SPEC.md §6.

Step 2 (array-indexed price levels) moves the needle less than step 1 did — expected, since `New`/`Delete`/`ExecuteVisible` p50 were already sitting near the timer's noise floor (0-42ns) after step 1, so there wasn't much p50 headroom left for a level-lookup optimization to reclaim. The gain shows up in throughput (~15.5-16.1M → ~21.3-22.1M msg/s) and tail latency (overall p99 125ns → 84ns, p99.9 375-416ns → 250-500ns) — consistent with removing red-black-tree pointer-chasing from `add()`'s level lookup and `execute()`'s best-price walk, both of which matter more for tail behavior than for an already-fast median. Full test suite still 38/38; full-day invariant replay still 0 violations.

**Step 3 (intrusive linked lists) is a small, honest regression**, not an improvement: throughput ~21.3-22.1M → ~19.5-20.7M msg/s, p99.9 250-500ns → 333-792ns, both confirmed stable across 5+ repeated runs (not noise). The reason: this step traded `std::list<Order>`'s allocator-managed nodes for individually heap-allocated `Order` nodes via plain `new`/`delete` per `add()`/`cancel()`/`reduce()`-to-zero/`execute()`-fill — and `std::list`'s allocator was *already* doing effectively the same one-allocation-per-node work, so removing the "container wrapper" indirection bought nothing, while the raw `new`/`delete` calls (no pooling yet) are exposed directly on the hot path with no offsetting win. This is exactly the gap step 4 (memory pool) is meant to close — pre-allocating nodes should recover this regression and then some, since the pool amortizes the allocation cost this step introduced. Kept as its own row rather than silently folded into step 4's benchmark, per SPEC.md's "one optimization at a time" rule and Phase 7's honesty requirement: the array step's win shouldn't be allowed to mask this step's loss in the final table. Full test suite still 38/38 (including a debug ASan+UBSan build — `-fsanitize=address,undefined` — to check the new manual heap-node lifetime for leaks/use-after-free; clean). Full-day invariant replay still 0 violations.

**Step 4 (memory pool) recovers step 3's regression and then some**: ~19.5-20.7M → ~22.1-22.8M msg/s — the best throughput of any step so far, edging out even step 2's array-only number. More strikingly, it's also by far the most *stable* result in the whole table: p99.9 sits in a tight 167-209ns band across 5 repeated runs, versus 250-500ns (step 2) and 333-792ns (step 3) — the wide run-to-run swings in earlier steps were `malloc`/`free`'s own variance (allocator lock contention, free-list search, page faults on growth) showing up in the tail; a bump allocator with an O(1) intrusive free list has none of that. This confirms the step 3 regression's diagnosis was right: it was allocation overhead, not the intrusive-pointer design itself, and pooling — not reverting to `std::list` — was the correct fix. `OrderBook` simplified too: since `OrderPool`'s `std::unique_ptr<Order[]>` chunks now own all node memory, the manual per-order `delete`-walking destructor step 3 added is gone (`~OrderBook() = default` — chunk teardown frees everything). Full test suite still 38/38 including ASan+UBSan; full-day invariant replay still 0 violations.

**Step 5 (cache alignment) shows no measurable benefit here — a flat-to-slightly-worse result**: ~22.1-22.8M → ~21.4-22.0M msg/s, confirmed stable (not noise) across 8 repeated runs; p50/p99/p99.9 essentially unchanged. Recorded honestly rather than reframed as a win, per Phase 7's honesty requirement. Two reasons this optimization doesn't pay off in this codebase: (1) "reduce false sharing" — half the step's stated goal (SPEC.md §3) — has no target to hit, since this is a single-threaded matching engine with no concurrent access to `Order` from different cores; false sharing is a multi-threaded phenomenon by definition. (2) "align to cache lines" still applies in principle (avoiding an order straddling two 64-byte lines), but `Order` was already 48 bytes before this change — mostly, though not always, fitting in one line — and padding it to 64 is a 33% larger memory footprint for every resting order, which increases the total bytes `OrderPool`'s chunks and the LOBSTER replay's working set touch. For this workload (each message reads/writes only 1-2 orders, not long sequential scans across many), the memory-traffic cost of the padding outweighs the occasional-straddle savings it buys. The general technique isn't wrong — it's a legitimate win in workloads with true cross-core contention or large sequential per-order scans — it just doesn't match this project's access pattern. Left in the codebase as the final table row rather than reverted, since the point of Phase 6 was measuring each candidate honestly, not cherry-picking wins; a real limit-order-book implementation with multi-threaded market-data fan-out is exactly where this would start to matter, and that's noted in Phase 7's limitations section (§ to be written). Full test suite still 38/38 including ASan+UBSan; full-day invariant replay still 0 violations.

---

## 8. Toolchain

| | |
|---|---|
| Local | macOS, Apple Clang, C++20 |
| CI | GitHub Actions — macOS *and* Ubuntu/gcc |
| Build | CMake (`cmake -S . -B build`, `cmake --build build`, `ctest --test-dir build`) |
| Tests | GoogleTest via `FetchContent`, pinned commit |
| Warnings | `-Wall -Wextra -Wpedantic` everywhere; `-Werror` scoped to the `orderbook` target (PRIVATE), applied locally and in CI alike — not CI-only, and not applied to third-party deps pulled in via FetchContent |
| Repo memory | `./CLAUDE.md` holding settled decisions, so Claude Code sessions don't re-litigate them |

Commit style: imperative mood, ≤50-char subject, body explaining *why* when the commit
encodes a decision.

---

## 9. Reference material

- **LOBSTER** — sample ITCH data and published limit order book snapshots
- **Nasdaq TotalView-ITCH 5.0 specification** — official message format documentation
- **WK Selph's blog post on order book implementation** — the canonical short reference
  on the array-of-price-levels + intrusive list design
- **Google Benchmark** — if microbenchmarking individual operations becomes useful
  alongside end-to-end throughput
