# CLAUDE.md

Repo memory for Claude Code sessions on this project. Full context lives in
`SPEC.md` — read it first if you haven't. This file exists so settled
decisions aren't re-litigated or accidentally violated across sessions.

## Working constraints (binding — see SPEC.md §2)

- **No skipping phases.** Correctness (hand-verified cases, invariants,
  real-data validation) comes before optimization, always. Don't suggest
  benchmarking or optimizing ahead of where SPEC.md's phase roadmap (§3) says
  the project currently is.
- **One optimization at a time, benchmarked.** Never bundle two performance
  changes into one step — it breaks attribution in the results table.
- **Settled decisions get written down** in SPEC.md §4/§4b as they're made.
  Check there before proposing a type, API shape, or design that SPEC.md may
  have already settled.

## Toolchain quick reference (see SPEC.md §8 for rationale)

- Local: macOS, Apple Clang, C++20. CI: GitHub Actions, macOS + Ubuntu/gcc.
- Build: `cmake -S . -B build`, `cmake --build build`, `ctest --test-dir build`.
- Tests: GoogleTest via `FetchContent`, pinned to `v1.15.2`.
- Warnings: `-Wall -Wextra -Wpedantic` applied globally (top-level
  `CMakeLists.txt`); `-Werror` scoped `PRIVATE` to the `orderbook` target only
  (`src/CMakeLists.txt`) — applies locally and in CI, deliberately excluded
  from FetchContent'd third-party code.
- Python prototype lives in `python/` and was the correctness reference during
  design — not something to modify casually.

## Current phase

Phase 5 (benchmark baseline) complete — see SPEC.md §3/§7. Baseline:
~660-710k msg/s, 125ns overall p50, measured via `tools/benchmark` against
the full real AAPL 2012-06-21 LOBSTER day in a separate Release build
(`build-release/`, gitignored — `cmake -S . -B build-release
-DCMAKE_BUILD_TYPE=Release`). `add()` is ~50-60x faster than
`cancel()`/`reduce()` at p50 because the latter do a linear order-ID scan
with no index — sets up Phase 6 step 3 (O(1) cancel) as the clear first
optimization target. Phase 4 (real market data) is also complete — see §4c
for the LOBSTER windowed-sample finding. Sample data lives in
`data/lobster/` (gitignored — download via the Hugging Face mirror
`totalorganfailure/lobster-data`). Next up: Phase 6 (optimization) — see
SPEC.md §3, and CLAUDE.md's binding constraint above: one optimization at a
time, benchmarked against this baseline, before moving to the next.

## Settled decisions

See SPEC.md §4 (Python prototype) and §4b (C++ port). Don't duplicate that
table here — it drifts. Link to it instead.
