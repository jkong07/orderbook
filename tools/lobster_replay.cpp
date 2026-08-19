// Standalone CLI: replays a LOBSTER message file through OrderBook and
// validates every resulting book state against the matching orderbook
// snapshot file. Not part of the test suite (the full-day sample files are
// too large to check into git — see data/lobster/ in .gitignore); run it
// manually against a downloaded sample. See SPEC.md §4b.
//
// Usage: lobster_replay <message_file.csv> <orderbook_file.csv>

#include "orderbook/book.hpp"
#include "orderbook/invariants.hpp"
#include "orderbook/lobster.hpp"
#include "orderbook/lobster_validate.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void accumulate(orderbook::Tally& tally, const orderbook::lobster::ApplyEffect& effect) {
    tally.added_buy += effect.added_buy;
    tally.cancelled_buy += effect.cancelled_buy;
    tally.filled_buy += effect.filled_buy;
    tally.added_sell += effect.added_sell;
    tally.cancelled_sell += effect.cancelled_sell;
    tally.filled_sell += effect.filled_sell;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: lobster_replay <message_file.csv> <orderbook_file.csv>\n";
        return EXIT_FAILURE;
    }

    const std::string message_path = argv[1];
    const std::string orderbook_path = argv[2];

    std::ifstream orderbook_file(orderbook_path);
    if (!orderbook_file) {
        std::cerr << "could not open orderbook file: " << orderbook_path << '\n';
        return EXIT_FAILURE;
    }

    orderbook::OrderBook book;
    orderbook::Tally tally;
    std::size_t line_number = 0;
    std::size_t snapshot_mismatches = 0;
    std::size_t invariant_violations = 0;
    std::string snapshot_line;

    orderbook::lobster::for_each_message(message_path, [&](const orderbook::lobster::Message& message) {
        ++line_number;
        accumulate(tally, orderbook::lobster::apply(book, message));

        // check_not_crossed is deliberately excluded here: LOBSTER logs an
        // Add and its matching Execute(s) as separate rows, so a marketable
        // order can legitimately lock/cross the book for one message before
        // the following Execute row consumes it. See SPEC.md §4b.
        std::optional<std::string> violation = orderbook::check_positive_qty(book);
        if (!violation) {
            violation = orderbook::check_unique_ids(book);
        }
        if (!violation) {
            violation = orderbook::check_conservation(book, tally);
        }
        if (violation) {
            if (invariant_violations == 0) {
                std::cerr << "line " << line_number << ": INVARIANT VIOLATED: " << *violation << '\n';
            }
            ++invariant_violations;
        }

        if (!std::getline(orderbook_file, snapshot_line)) {
            std::cerr << "line " << line_number << ": orderbook file ran out of rows\n";
            std::exit(EXIT_FAILURE);
        }

        const orderbook::lobster::BookSnapshot snapshot =
            orderbook::lobster::parse_orderbook_line(snapshot_line);
        if (auto mismatch = orderbook::lobster::compare_to_snapshot(book, snapshot)) {
            if (snapshot_mismatches == 0) {
                std::cerr << "line " << line_number << ": first snapshot mismatch: " << mismatch->detail
                           << '\n';
            }
            ++snapshot_mismatches;
        }
    });

    std::cout << line_number << " messages replayed, " << invariant_violations
               << " invariant violations, " << snapshot_mismatches << " snapshot mismatches ("
               << (line_number == 0 ? 0.0
                                     : 100.0 * static_cast<double>(line_number - snapshot_mismatches) /
                                           static_cast<double>(line_number))
               << "% of rows matched)\n";

    // Snapshot mismatches are an expected, documented consequence of
    // LOBSTER's windowed sample files (see SPEC.md §4b) — not treated as
    // failure here. Invariant violations are a real bug and do fail the run.
    return invariant_violations == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
