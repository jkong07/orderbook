#pragma once

#include "orderbook/types.hpp"

#include <vector>

namespace orderbook {

struct Fill {
    OrderId resting_id;
    Price price;
    Qty qty;
};

// Returned by OrderBook::execute() instead of mutating the incoming order's
// qty in place — see SPEC.md §4b for why.
struct ExecuteResult {
    std::vector<Fill> fills;
    Qty remaining_qty;
};

} // namespace orderbook
