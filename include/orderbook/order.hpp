#pragma once

#include "orderbook/types.hpp"

namespace orderbook {

struct Order {
    OrderId order_id;
    Side side;
    Price price;
    Qty qty;
};

} // namespace orderbook
