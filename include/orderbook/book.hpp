#pragma once

#include "orderbook/fill.hpp"
#include "orderbook/order.hpp"
#include "orderbook/types.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace orderbook {

class OrderBook {
public:
    // Bids and asks are different types (see SPEC.md §4b): both iterate
    // best-price-first from begin() — bids via std::greater, asks via the
    // default ascending order — so callers never need to reverse-iterate.
    using BidLevels = std::map<Price, std::deque<Order>, std::greater<Price>>;
    using AskLevels = std::map<Price, std::deque<Order>>;

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
    BidLevels bids_;
    AskLevels asks_;
    OrderId next_seq_ = 0;
};

} // namespace orderbook
