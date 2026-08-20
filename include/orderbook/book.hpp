#pragma once

#include "orderbook/fill.hpp"
#include "orderbook/intrusive_order_list.hpp"
#include "orderbook/order.hpp"
#include "orderbook/order_pool.hpp"
#include "orderbook/price_level_array.hpp"
#include "orderbook/types.hpp"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace orderbook {

class OrderBook {
public:
    // Intrusive list (not std::list) — orders carry their own prev/next
    // pointers (Order::prev/next), so OrderBook owns each resting order as
    // an individually heap-allocated node instead of a value living inside
    // a container-owned node (SPEC.md §4b Phase 6 step 2). index_ holds
    // iterators wrapping those nodes' addresses directly, still O(1) to
    // resolve for cancel/reduce (step 3).
    using OrderList = IntrusiveOrderList;

    // Bids and asks are different types (see SPEC.md §4b): both iterate
    // best-price-first from begin() — bids descending (highest price
    // first), asks ascending (lowest price first) — so callers never need
    // to reverse-iterate. Flat arrays indexed by price offset, not
    // std::map, since SPEC.md §3 Phase 6 step 1 — see price_level_array.hpp.
    using BidLevels = BidLevelArray;
    using AskLevels = AskLevelArray;

    OrderBook() = default;
    // Resting orders live in pool_'s chunks (see OrderPool) and are owned
    // outright, no aliasing — a shallow copy would let both books' nodes
    // point into the same pool, so copying is disabled rather than
    // implemented (pool_'s own non-copyable members would delete this
    // implicitly anyway; explicit for clarity). Moving is fine.
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&) = default;
    OrderBook& operator=(OrderBook&&) = default;
    ~OrderBook() = default;

    // Monotonic ID source for callers to mint order IDs with before
    // constructing an Order. add() does not assign IDs itself.
    OrderId next_id();

    // Takes ownership of `order` (moved into the book). Returns its order_id.
    OrderId add(Order order);

    // Removes and returns the order with the given ID, or nullopt if not
    // found. Prunes the price level if it empties out.
    std::optional<Order> cancel(OrderId order_id);

    // Shrinks the resting order with the given ID by `qty` in place
    // (preserves its time priority — unlike cancel+re-add). If the
    // reduction consumes the order entirely (qty >= order's remaining
    // qty), the order is removed, same as cancel(). Returns the order as
    // it exists after the reduction (qty already updated), or nullopt if
    // not found. Prunes the price level if it empties out. See SPEC.md
    // §4b — added for LOBSTER partial-cancel/execution replay, where a
    // resting order shrinks without changing its queue position.
    std::optional<Order> reduce(OrderId order_id, Qty qty);

    // Matches `order` against the opposite side (price-time priority).
    // Does not mutate the caller's order — see ExecuteResult in fill.hpp.
    ExecuteResult execute(Order order);

    // Aggregated resting qty per price level, best-first on both sides.
    // n = std::nullopt means "all levels".
    std::pair<std::vector<std::pair<Price, Qty>>, std::vector<std::pair<Price, Qty>>>
    depth(std::optional<std::size_t> n = std::nullopt) const;

    void printer(const std::vector<std::pair<Price, Qty>>& bid_list,
                 const std::vector<std::pair<Price, Qty>>& ask_list) const;

    // Read-only views of resting orders, best-first on both sides — for
    // invariant checks (see invariants.hpp) that need per-order data depth()
    // doesn't expose (individual order_id/qty, not aggregated by level).
    const BidLevels& bids() const { return bids_; }
    const AskLevels& asks() const { return asks_; }

private:
    // Order-ID -> resting location, so cancel()/reduce() skip the linear
    // scan across price levels (see SPEC.md §4b Phase 6 step 3). Every
    // add() inserts an entry; every erase()/pop of a resting order
    // (cancel(), reduce()-to-zero, execute() consuming a resting order)
    // must remove it.
    struct IndexEntry {
        Side side;
        Price price;
        OrderList::iterator it;
    };

    BidLevels bids_;
    AskLevels asks_;
    std::unordered_map<OrderId, IndexEntry> index_;
    OrderPool pool_;
    OrderId next_seq_ = 0;
};

} // namespace orderbook
