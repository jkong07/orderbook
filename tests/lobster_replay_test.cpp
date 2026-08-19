#include "orderbook/book.hpp"
#include "orderbook/invariants.hpp"
#include "orderbook/lobster.hpp"
#include "orderbook/lobster_validate.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

namespace orderbook::lobster {
namespace {

// A short real slice of AAPL_2012-06-21_..._10.csv (LOBSTER free sample,
// see SPEC.md §4b) — the window before pre-existing (pre-9:30) resting
// orders start getting touched by messages this window never saw the birth
// of. See tools/lobster_replay.cpp for full-day replay against the actual
// downloaded sample (not checked in — too large for git).
constexpr const char* kMessageFixture = "fixtures/lobster_aapl_message.csv";
constexpr const char* kOrderbookFixture = "fixtures/lobster_aapl_orderbook.csv";

std::vector<std::string> read_lines(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open fixture: " + path);
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

TEST(LobsterReplayTest, ReplayMatchesSnapshotsExactlyForShortRealWindow) {
    const std::vector<std::string> message_lines = read_lines(kMessageFixture);
    const std::vector<std::string> orderbook_lines = read_lines(kOrderbookFixture);
    ASSERT_EQ(message_lines.size(), orderbook_lines.size());
    ASSERT_GT(message_lines.size(), 0u);

    // Seed the book with the true pre-window state, reconstructed by
    // undoing the first message's own effect on the first snapshot row
    // (see SPEC.md §4b — the free LOBSTER sample's message file starts
    // after resting liquidity already existed).
    const Message first_message = parse_message_line(message_lines.front());
    const BookSnapshot first_row = parse_orderbook_line(orderbook_lines.front());
    const BookSnapshot seed = snapshot_before(first_row, first_message);

    OrderBook book;
    Tally tally;
    seed_from_snapshot(book, seed);
    for (const auto& [price, qty] : seed.bids) {
        tally.added_buy += qty;
    }
    for (const auto& [price, qty] : seed.asks) {
        tally.added_sell += qty;
    }

    for (std::size_t i = 0; i < message_lines.size(); ++i) {
        const Message message = parse_message_line(message_lines[i]);
        const ApplyEffect effect = apply(book, message);
        tally.added_buy += effect.added_buy;
        tally.cancelled_buy += effect.cancelled_buy;
        tally.filled_buy += effect.filled_buy;
        tally.added_sell += effect.added_sell;
        tally.cancelled_sell += effect.cancelled_sell;
        tally.filled_sell += effect.filled_sell;

        ASSERT_EQ(check_positive_qty(book), std::nullopt) << "at message " << (i + 1);
        ASSERT_EQ(check_unique_ids(book), std::nullopt) << "at message " << (i + 1);
        ASSERT_EQ(check_conservation(book, tally), std::nullopt) << "at message " << (i + 1);

        const BookSnapshot snapshot = parse_orderbook_line(orderbook_lines[i]);
        auto mismatch = compare_to_snapshot(book, snapshot);
        ASSERT_EQ(mismatch, std::nullopt)
            << "at message " << (i + 1) << ": " << (mismatch ? mismatch->detail : "");
    }
}

} // namespace
} // namespace orderbook::lobster
