#pragma once

#include "orderbook/book.hpp"
#include "orderbook/lobster.hpp"
#include "orderbook/types.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace orderbook::lobster {

// One parsed row of a LOBSTER orderbook-file snapshot, ask/bid price+size
// pairs for as many levels as the file contains, best-first (matches
// OrderBook::depth()'s ordering).
struct BookSnapshot {
    std::vector<std::pair<Price, Qty>> asks;
    std::vector<std::pair<Price, Qty>> bids;
};

// Parses one LOBSTER orderbook-file line: repeating groups of
// (AskPrice, AskSize, BidPrice, BidSize), one group per level. Prices are
// converted to cents the same way as parse_message_line(). A level with no
// resting order on a side is encoded by LOBSTER as price -9999999999 (ask
// side empty) or 9999999999 (bid side empty) with size 0; such levels are
// omitted from the returned snapshot, matching depth()'s "no empty levels"
// invariant.
BookSnapshot parse_orderbook_line(const std::string& line);

// Describes the first mismatch found between a replayed OrderBook's depth
// and a LOBSTER snapshot, or nullopt if they agree (see
// compare_to_snapshot()).
struct Mismatch {
    std::string detail;
};

// Compares `book`'s current depth (limited to the snapshot's own level
// count) against `snapshot`. Returns nullopt if every level matches on both
// sides, else a Mismatch describing the first divergence.
std::optional<Mismatch> compare_to_snapshot(const OrderBook& book, const BookSnapshot& snapshot);

// Seeds `book` with one synthetic resting order per (price, qty) pair in
// `snapshot`, using negative order IDs starting at -1 and counting down
// (real LOBSTER order IDs are always positive, so these can never collide
// with orders added later via apply()). Collapsing each price level into a
// single order loses whatever individual-order breakdown truly existed —
// see SPEC.md §4b on why that breakdown is unrecoverable from a windowed
// LOBSTER sample, and why this seeding only buys exact-match validation for
// a short window before that loss of fidelity causes drift.
void seed_from_snapshot(OrderBook& book, const BookSnapshot& snapshot);

// Reconstructs the book snapshot implied immediately before `first_message`,
// by undoing its effect on `first_row_snapshot` (the snapshot taken right
// after applying it). Only EventType::New is supported — that's the only
// case this project's fixtures need — throws std::invalid_argument
// otherwise.
BookSnapshot snapshot_before(const BookSnapshot& first_row_snapshot, const Message& first_message);

} // namespace orderbook::lobster
