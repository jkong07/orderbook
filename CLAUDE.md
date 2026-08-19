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

Phase 4 (real market data) complete — see SPEC.md §3/§4b/§4c. LOBSTER
message parsing, event-type dispatch, and invariant checks (ported from
`python/invariants.py`) are done; full AAPL 2012-06-21 trading day replays
with 0 invariant violations. Along the way, discovered and documented (§4c)
that free LOBSTER samples can't support exact full-day snapshot
reconstruction (a data-source limitation, not an `OrderBook` bug) — exact
snapshot-match validation is scoped to a short real window instead. Sample
data lives in `data/lobster/` (gitignored, not committed — download via the
Hugging Face mirror `totalorganfailure/lobster-data`, see §4c). Next up:
Phase 5 (benchmark baseline) — see SPEC.md §3.

## Settled decisions

See SPEC.md §4 (Python prototype) and §4b (C++ port). Don't duplicate that
table here — it drifts. Link to it instead.
