#pragma once

#include <cstdint>

namespace orderbook {

// See SPEC.md §4b for the rationale behind each of these.
using Price = std::int64_t;
using Qty = std::int64_t;
using OrderId = std::int64_t;

enum class Side : std::uint8_t { Buy, Sell };

} // namespace orderbook
