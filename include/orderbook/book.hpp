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

    // Matches `order` against the opposite side (price-time priority).
    // Does not mutate the caller's order — see ExecuteResult in fill.hpp.
    ExecuteResult execute(Order order);

    // Aggregated resting qty per price level, best-first on both sides.
    // n = std::nullopt means "all levels".
    std::pair<std::vector<std::pair<Price, Qty>>, std::vector<std::pair<Price, Qty>>>
    depth(std::optional<std::size_t> n = std::nullopt) const;

    void printer(const std::vector<std::pair<Price, Qty>>& bid_list,
                 const std::vector<std::pair<Price, Qty>>& ask_list) const;

private:
    BidLevels bids_;
    AskLevels asks_;
    OrderId next_seq_ = 0;
};

} // namespace orderbook
