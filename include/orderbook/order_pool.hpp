#pragma once

#include "orderbook/order.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace orderbook {

// Bump-allocates Order nodes out of fixed-size chunks and recycles
// released ones via a free list, so OrderBook's hot path (add/cancel/
// reduce/execute) never calls malloc/free directly — SPEC.md §3 Phase 6
// step 4, replacing step 2's plain new/delete (which turned out to be a
// measured regression — see §7). Does not track which nodes are live:
// whoever calls acquire()/release() (OrderBook) owns that bookkeeping,
// same division of responsibility as IntrusiveOrderList not owning memory.
class OrderPool {
public:
    Order* acquire(Order order) {
        Order* node = free_list_ ? pop_free() : bump_allocate();
        *node = std::move(order);
        node->prev = nullptr;
        node->next = nullptr;
        return node;
    }

    // `node` must have come from this pool's acquire() and not already be
    // released. Reuses Order::next as the free-list link — safe because a
    // released node isn't resting in any IntrusiveOrderList, so nothing
    // else is reading its prev/next.
    void release(Order* node) {
        node->next = free_list_;
        free_list_ = node;
    }

private:
    // 4096 orders/chunk: large enough that chunk allocation is rare
    // relative to message volume (the full AAPL trading day used for
    // benchmarking has ~187k adds — see SPEC.md §7 — so under 50 chunk
    // allocations total), small enough that an unused tail chunk isn't a
    // wasteful over-allocation.
    static constexpr std::size_t kChunkSize = 4096;

    Order* pop_free() {
        Order* node = free_list_;
        free_list_ = free_list_->next;
        return node;
    }

    Order* bump_allocate() {
        if (chunks_.empty() || chunk_cursor_ == kChunkSize) {
            chunks_.push_back(std::make_unique<Order[]>(kChunkSize));
            chunk_cursor_ = 0;
        }
        return &chunks_.back()[chunk_cursor_++];
    }

    std::vector<std::unique_ptr<Order[]>> chunks_;
    std::size_t chunk_cursor_ = 0;
    Order* free_list_ = nullptr;
};

} // namespace orderbook
