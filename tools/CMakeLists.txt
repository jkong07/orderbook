# Manual-use CLI tools, not exercised by ctest — see lobster_replay.cpp for
# why (needs the full-day LOBSTER sample files, which aren't checked in).
add_executable(lobster_replay
    lobster_replay.cpp
)

target_link_libraries(lobster_replay PRIVATE
    orderbook
)

# See benchmark.cpp for why this also isn't part of ctest — timing numbers
# from a shared CI runner would be noise, not signal.
add_executable(benchmark
    benchmark.cpp
)

target_link_libraries(benchmark PRIVATE
    orderbook
)
