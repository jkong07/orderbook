# FetchContent downloads and configures a dependency's source at CMake
# configure time, then makes its targets available as if they were a
# subdirectory of this project — no submodule bookkeeping, no need for the
# dependency to already be installed on the machine. Tradeoff: first
# configure needs network access and is slower (clones GoogleTest), and
# every clean build re-resolves it unless you enable FetchContent's cache.
include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2 # pin a release tag so builds are reproducible; bump
                   # deliberately rather than tracking a branch.
)

# GoogleTest links the C runtime dynamically by default on Windows; this
# forces it to match our own build's CRT setting. It's a no-op on
# macOS/Linux but keeps this file portable if you ever build on Windows.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Actually performs the download/configure and defines GoogleTest's targets
# (GTest::gtest, GTest::gtest_main, etc.) for us to link against below.
FetchContent_MakeAvailable(googletest)

# The test binary. Add new test .cpp files to this list as you write them.
add_executable(orderbook_tests
    trivial_test.cpp
    order_book_test.cpp
)

target_link_libraries(orderbook_tests PRIVATE
    orderbook
    GTest::gtest_main
)

# gtest_discover_tests() (from GoogleTest's CMake package) runs the compiled
# test binary at build time to enumerate its individual TEST() cases and
# registers each one as its own CTest test — so `ctest` output lists them
# separately instead of one pass/fail for the whole binary. Alternative:
# add_test(NAME orderbook_tests COMMAND orderbook_tests) registers the whole
# binary as a single CTest test, which is simpler but gives coarser
# pass/fail reporting.
include(GoogleTest)
gtest_discover_tests(orderbook_tests)
