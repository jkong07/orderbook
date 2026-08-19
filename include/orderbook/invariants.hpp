#pragma once

#include "orderbook/book.hpp"
#include "orderbook/types.hpp"

#include <optional>
#include <string>

namespace orderbook {

// Running per-side tallies of quantity that has entered/left the book, for
// check_conservation(). Update alongside add()/cancel()/reduce()/execute()
// calls — see lobster::apply() (lobster.cpp) for an example driver.
//
// `filled` is counted once per trade, on the resting side only: a fill
// removes qty from a resting order (tallied here) and from the aggressive
// incoming order (never added to the book in the first place, so it isn't
// tallied at all). See python/invariants.py's check_conservation for the
// same convention in the prototype.
struct Tally {
    Qty added_buy = 0;
    Qty cancelled_buy = 0;
    Qty filled_buy = 0;
    Qty added_sell = 0;
    Qty cancelled_sell = 0;
    Qty filled_sell = 0;
};

// Ported from python/invariants.py (see SPEC.md §6). Each check returns a
// description of the violation, or nullopt if it holds. Checks that are
// structurally guaranteed by OrderBook's own implementation and can't be
// violated externally (no empty levels, price/level-key consistency, FIFO
// ordering within a level) are deliberately not ported — only checks that
// catch a real class of driver bug are.

// Best bid must be strictly below best ask (an empty side trivially holds).
std::optional<std::string> check_not_crossed(const OrderBook& book);

// Every resting order's qty must be strictly positive.
std::optional<std::string> check_positive_qty(const OrderBook& book);

// No order_id may appear more than once across both sides of the book.
std::optional<std::string> check_unique_ids(const OrderBook& book);

// Resting qty per side must equal added - cancelled - filled for that side.
std::optional<std::string> check_conservation(const OrderBook& book, const Tally& tally);

// Runs all of the above; returns the first violation found, if any.
std::optional<std::string> check_all(const OrderBook& book, const Tally& tally);

} // namespace orderbook
