# CLAUDE.md

Repo memory for Claude Code sessions on this project. Full context lives in
`SPEC.md` — read it first if you haven't. This file exists so settled
decisions aren't re-litigated or accidentally violated across sessions.

## Working constraints (binding — see SPEC.md §2)

- **All core logic is hand-written by the user.** Data structures, matching
  logic, the ITCH parser, and every optimization are written by hand. Claude's
  role is explanation, references, review, and scaffolding (build system, CI,
  test harness plumbing) — **not** producing the order book implementation
  itself. If asked to write book/matching logic, flag this constraint rather
  than just doing it.
- **No skipping phases.** Correctness (hand-verified cases, invariants,
  differential testing, real-data validation) comes before optimization,
  always. Don't suggest benchmarking or optimizing ahead of where SPEC.md's
  phase roadmap (§3) says the project currently is.
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
- Python prototype lives in `python/` and is the correctness reference for
  differential testing — not something to modify casually.

## Current phase

Phase 3 (C++ port) in progress. Build scaffolding (CMake, GoogleTest, CI) and
the C++ type/API decisions (SPEC.md §4b) are both done. Next up: hand-verified
sequences ported to GoogleTest cases, written before the implementation —
see SPEC.md §3 Phase 3 for the full remaining checklist.

## Settled decisions

See SPEC.md §4 (Python prototype) and §4b (C++ port). Don't duplicate that
table here — it drifts. Link to it instead.
