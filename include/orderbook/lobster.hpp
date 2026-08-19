#pragma once

#include "orderbook/book.hpp"
#include "orderbook/types.hpp"

#include <functional>
#include <string>

namespace orderbook::lobster {

// LOBSTER message-file event codes. See SPEC.md §4b — Cross (6) and Halt (7)
// carry sentinel values instead of a real order event and are passed through
// unapplied; ExecuteHidden (5) refers to an order that was never in the
// visible book and is also passed through unapplied.
enum class EventType : int {
    New = 1,
    PartialCancel = 2,
    Delete = 3,
    ExecuteVisible = 4,
    ExecuteHidden = 5,
    Cross = 6,
    Halt = 7,
};

// One row of a LOBSTER message file, fields as documented at
// https://lobsterdata.com/info/DataStructure.php. `price` is converted from
// LOBSTER's dollars-times-10000 fixed point to this project's cents (§4b);
// `side` is converted from LOBSTER's Direction (1 = buy, -1 = sell).
struct Message {
    double time_seconds;
    EventType type;
    OrderId order_id;
    Qty size;
    Price price;
    Side side;
};

// Parses one LOBSTER message-file line (no header row, 6 comma-separated
// fields). Throws std::invalid_argument on malformed input.
Message parse_message_line(const std::string& line);

// Streams a LOBSTER message file line by line, invoking `callback` for each
// parsed Message in file order. Throws std::runtime_error if the file can't
// be opened.
void for_each_message(const std::string& path, const std::function<void(const Message&)>& callback);

// Qty moved by one apply() call, broken out by side and by which of
// add/cancel/fill it went through — feeds directly into a Tally
// (invariants.hpp) for conservation checking. Exactly one of added_*,
// cancelled_*, filled_* is non-zero per call (zero for the no-op event
// types, or when the message targets an order this replay never saw added
// — see SPEC.md §4b on LOBSTER's windowed-sample limitation).
struct ApplyEffect {
    Qty added_buy = 0;
    Qty cancelled_buy = 0;
    Qty filled_buy = 0;
    Qty added_sell = 0;
    Qty cancelled_sell = 0;
    Qty filled_sell = 0;
};

// Applies one LOBSTER message to `book` per the event-type rules in SPEC.md
// §4b: New -> add(), PartialCancel/ExecuteVisible -> reduce(), Delete ->
// cancel(). ExecuteHidden, Cross, and Halt are no-ops. Returns the effect on
// `book`'s resting qty, for conservation tracking.
ApplyEffect apply(OrderBook& book, const Message& message);

} // namespace orderbook::lobster
