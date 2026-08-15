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

Phase 3 (C++ port) complete: build scaffolding, type/API decisions (SPEC.md
§4b), and hand-verified sequences ported to GoogleTest cases (all passing) are
all done. A Python/C++ differential test harness was considered and
deliberately cut (see SPEC.md §6) — hand-verified cases plus Phase 4's
real-data validation were judged sufficient. Next up: Phase 4 (real market
data) — see SPEC.md §3.

## Settled decisions

See SPEC.md §4 (Python prototype) and §4b (C++ port). Don't duplicate that
table here — it drifts. Link to it instead.
