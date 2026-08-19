# Minimum CMake version. 3.20 gives us reliable FetchContent + Apple Clang
# support; bump this if you start using a feature that needs a newer version
# (CMake will error out with a clear message telling you what's required).
cmake_minimum_required(VERSION 3.20)

project(orderbook LANGUAGES CXX)

# --- Language standard -------------------------------------------------
# CMAKE_CXX_STANDARD picks the standard; REQUIRED makes CMake fail instead of
# silently falling back to an older standard if the compiler can't do C++20.
# EXTENSIONS OFF disables compiler-specific extensions (e.g. GNU dialects),
# so `-std=c++20` is used instead of `-std=gnu++20` — keeps behavior portable
# across Apple Clang / GCC / MSVC.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emits compile_commands.json in the build directory, which clangd/IDEs use
# for autocomplete and go-to-definition. Harmless to leave on always.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Warnings ------------------------------------------------------------
# Applied via add_compile_options() at directory scope, so every target
# defined in this directory or added below via add_subdirectory() inherits
# them (targets defined *before* this line would not) — this includes
# third-party code pulled in via FetchContent (e.g. GoogleTest), so it's
# just the warning flags, not -Werror.
#
# -Werror is deliberately NOT set globally here — see src/CMakeLists.txt,
# where it's applied PRIVATE to just the `orderbook` target instead. That
# keeps your own code held to a stricter bar without risking a warning in
# dependency code (which you don't control) turning into a hard failure.
add_compile_options(-Wall -Wextra -Wpedantic)

# Enables `ctest` and the CTest integration that GoogleTest's
# gtest_discover_tests() (used in tests/CMakeLists.txt) relies on.
enable_testing()

# Each subdirectory owns its own target definitions — keeps this top-level
# file as a small, stable "policy" file (standard, warnings, subdirs) rather
# than growing target-specific logic here.
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(tools)
