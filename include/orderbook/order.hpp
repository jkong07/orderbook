#pragma once

#include "orderbook/types.hpp"

namespace orderbook {

// alignas(64): natural size is 48 bytes (SPEC.md §3 Phase 6 step 5), which
// doesn't divide a 64-byte cache line evenly — in a contiguous array (every
// Order lives in OrderPool's chunks, see order_pool.hpp), most elements
// would straddle two cache lines, costing a second cache-line fetch to
// read one order. Padding to 64 forces every order onto exactly one cache
// line. Field order otherwise doesn't matter here: at 48-64 bytes, the
// whole struct is "hot" — there's no cold tail field to push out to a
// separate line, so there was nothing to gain by reordering members.
struct alignas(64) Order {
    OrderId order_id;
    Side side;
    Price price;
    Qty qty;

    // Intrusive doubly-linked-list pointers for OrderBook's per-price-level
    // chain once an order is resting (SPEC.md §4b Phase 6 step 2) — replaces
    // std::list<Order>'s separately-allocated node. Always nullptr on an
    // Order the caller constructs; OrderBook (see intrusive_order_list.hpp)
    // owns and clears them, including on any Order handed back to a caller
    // (cancel()/reduce() results), so callers never observe a dangling link.
    Order* prev = nullptr;
    Order* next = nullptr;
};

} // namespace orderbook
