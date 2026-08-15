# Limit Order Book & Market-Data Feed Handler — Project Spec

**Repo:** `orderbook` (public, GitHub)
**Languages:** Python (prototype, complete) → C++20 (production implementation)
**Status as of 2026-08-14:** Phases 1–2 complete. Phase 3 (C++ port) in progress — build scaffolding done.

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

- Correct against hand-verified sequences, invariant checks, and randomized differential testing
- Correct against real Nasdaq ITCH data, validated against published LOBSTER snapshots
- A README table showing p50 / p99 / p99.9 latency and throughput across successive optimizations
- CI green on macOS and Linux from the first commit onward

---

## 2. Working constraints

These govern how the project is built, not just what it is.

- **All core logic is hand-written.** Data structures, matching logic, the ITCH parser,
  and every optimization are written by hand. AI assistance is for explanation,
  references, review, and scaffolding — not for producing the book itself.
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

### Phase 3 — C++ port 🔄 IN PROGRESS

Deliberately the naive implementation: `std::map<Price, std::deque<Order>>`. This is the
correctness baseline and the performance baseline. It is not meant to be fast.

- ✅ CMake project (C++20, `src/` + `include/` + `tests/`, `-Wall -Wextra -Wpedantic`)
- ✅ GoogleTest via `FetchContent`, pinned commit
- ✅ GitHub Actions CI building and testing on macOS and Ubuntu
- ✅ Type and API decisions settled (see §4b)
- ⬜ Hand-verified sequences ported to GoogleTest cases — *written before the implementation*
- ⬜ `Order` → empty book → `add` → `cancel` → `execute` → depth, in that order
- ⬜ Differential test harness: Python dumps a seeded event stream to file, C++ replays it,
  fill lists compared

### Phase 4 — Real market data ⬜

- Parse Nasdaq ITCH 5.0 via LOBSTER sample files
- Handle the message types needed to reconstruct the book (add, cancel, delete,
  execute, replace)
- Validate reconstructed book state against LOBSTER's published snapshots

This phase is where most naive implementations break. Real feeds contain message
sequences that synthetic tests never generate.

### Phase 5 — Benchmark baseline ⬜ *(resume-ready checkpoint)*

- Measure p50 / p99 / p99.9 per-message latency and overall throughput
- Establish measurement methodology before optimizing (see §7)
- README table with the baseline row filled in

At this point the project is presentable even if nothing further is done.

### Phase 6 — Optimization ⬜

In order, benchmarking after each and adding a row to the table:

1. **Array-indexed price levels** — replace `std::map` with a flat array indexed by
   price offset. Removes tree traversal and pointer chasing from the hot path.
2. **Intrusive linked lists** — orders carry their own list pointers rather than living
   in a `deque`. Removes a layer of indirection and allocation.
3. **O(1) cancel** — hash map from order ID directly to the order's node, so cancel
   stops being a scan.
4. **Memory pool** — pre-allocate order nodes; eliminate per-order allocation from the
   hot path.
5. **Cache alignment** — pack hot fields, align to cache lines, reduce false sharing.

### Phase 7 — Write-up ⬜

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

---

## 5. Open decisions for the C++ port

All items settled — see §4b. This section is kept as a placeholder in case new
decisions surface once book logic is actually being written.

---

## 6. Correctness strategy

Four independent layers, each catching what the others miss:

1. **Hand-predicted sequences** — outcomes worked out on paper before running. Covers
   both sides, limit-stopped walks, zero-fill, book exhaustion, cancel-after-partial-fill,
   same-price ordering, cancel from the middle of a level, cancel emptying a level.
   Catches misunderstandings of the domain.
2. **Invariants** — state invariants and the conservation invariant, checked after every
   operation in test builds. Catches corruption that produces plausible-looking output.
3. **Differential testing** — same seeded event stream through Python and C++, fill lists
   diffed. Catches everything the hand-written cases didn't think of, with an exact first
   divergence point. This is the strongest tool available and exists only because the
   prototype was built first.
4. **Real-data validation** — LOBSTER published snapshots. Catches domain assumptions
   that synthetic data never violates.

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
