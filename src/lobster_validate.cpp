#include "orderbook/lobster_validate.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace orderbook::lobster {

namespace {

// LOBSTER's sentinel for "no order at this level": ask side uses
// -9999999999, bid side uses 9999999999 (dollars * 10000, before this
// project's cents conversion).
constexpr long long kEmptyAskSentinel = -9999999999LL;
constexpr long long kEmptyBidSentinel = 9999999999LL;

Price to_cents(long long lobster_price) {
    return static_cast<Price>(lobster_price / 100);
}

} // namespace

BookSnapshot parse_orderbook_line(const std::string& line) {
    std::vector<long long> raw;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t comma = line.find(',', start);
        const std::string_view field =
            std::string_view(line).substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        raw.push_back(std::stoll(std::string(field)));
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }

    if (raw.size() % 4 != 0) {
        throw std::invalid_argument("lobster: malformed orderbook line: " + line);
    }

    BookSnapshot snapshot;
    const std::size_t num_levels = raw.size() / 4;
    for (std::size_t level = 0; level < num_levels; ++level) {
        const long long ask_price = raw[level * 4 + 0];
        const long long ask_size = raw[level * 4 + 1];
        const long long bid_price = raw[level * 4 + 2];
        const long long bid_size = raw[level * 4 + 3];

        if (ask_price != kEmptyAskSentinel) {
            snapshot.asks.emplace_back(to_cents(ask_price), ask_size);
        }
        if (bid_price != kEmptyBidSentinel) {
            snapshot.bids.emplace_back(to_cents(bid_price), bid_size);
        }
    }
    return snapshot;
}

namespace {

std::optional<Mismatch> compare_side(const std::string& side_name,
                                      const std::vector<std::pair<Price, Qty>>& actual,
                                      const std::vector<std::pair<Price, Qty>>& expected) {
    const std::size_t compare_levels = expected.size();
    if (actual.size() < compare_levels) {
        std::ostringstream out;
        out << side_name << ": book has only " << actual.size() << " levels, snapshot expects "
            << compare_levels;
        return Mismatch{out.str()};
    }

    for (std::size_t level = 0; level < compare_levels; ++level) {
        if (actual[level] != expected[level]) {
            std::ostringstream out;
            out << side_name << " level " << level << ": book has (" << actual[level].first << ", "
                << actual[level].second << "), snapshot expects (" << expected[level].first << ", "
                << expected[level].second << ")";
            return Mismatch{out.str()};
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<Mismatch> compare_to_snapshot(const OrderBook& book, const BookSnapshot& snapshot) {
    const std::size_t num_levels = std::max(snapshot.asks.size(), snapshot.bids.size());
    auto [bids, asks] = book.depth(num_levels);

    if (auto mismatch = compare_side("bid", bids, snapshot.bids)) {
        return mismatch;
    }
    return compare_side("ask", asks, snapshot.asks);
}

void seed_from_snapshot(OrderBook& book, const BookSnapshot& snapshot) {
    OrderId synthetic_id = -1;
    for (const auto& [price, qty] : snapshot.bids) {
        book.add(Order{synthetic_id--, Side::Buy, price, qty});
    }
    for (const auto& [price, qty] : snapshot.asks) {
        book.add(Order{synthetic_id--, Side::Sell, price, qty});
    }
}

namespace {

void undo_on_side(std::vector<std::pair<Price, Qty>>& side, Price price, Qty qty) {
    auto it = std::find_if(side.begin(), side.end(),
                            [price](const std::pair<Price, Qty>& level) { return level.first == price; });
    if (it == side.end() || it->second < qty) {
        throw std::invalid_argument("lobster: first_message's price/qty not present in first_row_snapshot");
    }
    it->second -= qty;
    if (it->second == 0) {
        side.erase(it);
    }
}

} // namespace

BookSnapshot snapshot_before(const BookSnapshot& first_row_snapshot, const Message& first_message) {
    if (first_message.type != EventType::New) {
        throw std::invalid_argument("lobster: snapshot_before only supports EventType::New");
    }

    BookSnapshot before = first_row_snapshot;
    if (first_message.side == Side::Buy) {
        undo_on_side(before.bids, first_message.price, first_message.size);
    } else {
        undo_on_side(before.asks, first_message.price, first_message.size);
    }
    return before;
}

} // namespace orderbook::lobster
