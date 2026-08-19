# The `orderbook` library target. STATIC (rather than INTERFACE) now that
# there's a real .cpp file to compile — the library produces an actual
# compiled artifact (liborderbook.a) that consumers link against, instead of
# just contributing include paths/flags.
add_library(orderbook STATIC
    book.cpp
    invariants.cpp
    lobster.cpp
    lobster_validate.cpp
)

# PUBLIC (not INTERFACE) because this include path is needed both to compile
# orderbook.cpp itself *and* by anything that links against this library —
# with INTERFACE it would only apply to consumers, and orderbook.cpp's own
# #include "orderbook/orderbook.hpp" would fail to resolve.
target_include_directories(orderbook PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(orderbook PUBLIC cxx_std_20)

# -Werror scoped to just this target (PRIVATE — doesn't propagate to
# consumers) rather than added globally in the top-level CMakeLists.txt.
# Global -Werror would also apply to third-party code pulled in via
# FetchContent (e.g. GoogleTest in tests/CMakeLists.txt); if a warning ever
# fires in dependency code you don't control, a global -Werror turns that
# into a build failure you can't fix. Scoping it to `orderbook` means only
# code you actually own is held to that stricter bar.
target_compile_options(orderbook PRIVATE -Werror)
