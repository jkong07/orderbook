#include "orderbook/book.hpp"
#include "orderbook/invariants.hpp"
#include "orderbook/order.hpp"
#include "orderbook/types.hpp"

#include <gtest/gtest.h>

namespace orderbook {
namespace {

// --- check_not_crossed -----------------------------------------------------

TEST(InvariantsTest, EmptyBookIsNotCrossed) {
    OrderBook book;
    EXPECT_EQ(check_not_crossed(book), std::nullopt);
}

TEST(InvariantsTest, OneSidedBookIsNotCrossed) {
    OrderBook book;
    book.add(Order{book.next_id(), Side::Buy, 10000, 10});
    EXPECT_EQ(check_not_crossed(book), std::nullopt);
}

TEST(InvariantsTest, NormalSpreadIsNotCrossed) {
    OrderBook book;
    book.add(Order{book.next_id(), Side::Buy, 10000, 10});
    book.add(Order{book.next_id(), Side::Sell, 10100, 10});
    EXPECT_EQ(check_not_crossed(book), std::nullopt);
}

TEST(InvariantsTest, BestBidAtOrAboveBestAskIsCrossed) {
    OrderBook book;
    book.add(Order{book.next_id(), Side::Buy, 10100, 10});
    book.add(Order{book.next_id(), Side::Sell, 10000, 10});
    EXPECT_NE(check_not_crossed(book), std::nullopt);
}

// --- check_positive_qty ------------------------------------------------------

TEST(InvariantsTest, PositiveQtyHoldsForNormalOrders) {
    OrderBook book;
    book.add(Order{book.next_id(), Side::Buy, 10000, 10});
    EXPECT_EQ(check_positive_qty(book), std::nullopt);
}

TEST(InvariantsTest, ZeroQtyOrderViolatesPositiveQty) {
    OrderBook book;
    book.add(Order{book.next_id(), Side::Buy, 10000, 0});
    EXPECT_NE(check_positive_qty(book), std::nullopt);
}

// --- check_unique_ids --------------------------------------------------------

TEST(InvariantsTest, DistinctIdsAreUnique) {
    OrderBook book;
    book.add(Order{1, Side::Buy, 10000, 10});
    book.add(Order{2, Side::Sell, 10100, 10});
    EXPECT_EQ(check_unique_ids(book), std::nullopt);
}

TEST(InvariantsTest, DuplicateIdAcrossSidesViolatesUniqueness) {
    OrderBook book;
    book.add(Order{1, Side::Buy, 10000, 10});
    book.add(Order{1, Side::Sell, 10100, 10});
    EXPECT_NE(check_unique_ids(book), std::nullopt);
}

// --- check_conservation ------------------------------------------------------

TEST(InvariantsTest, ConservationHoldsWhenTallyMatchesResting) {
    OrderBook book;
    book.add(Order{book.next_id(), Side::Buy, 10000, 40});
    book.cancel(0);
    book.add(Order{book.next_id(), Side::Buy, 10000, 30});

    Tally tally;
    tally.added_buy = 70;
    tally.cancelled_buy = 40;

    EXPECT_EQ(check_conservation(book, tally), std::nullopt);
}

TEST(InvariantsTest, ConservationViolatedWhenTallyUndercountsCancellation) {
    OrderBook book;
    book.add(Order{book.next_id(), Side::Buy, 10000, 40});
    book.cancel(0);

    Tally tally;
    tally.added_buy = 40;
    // cancelled_buy left at 0 despite the cancel above — tally says 40
    // should still be resting, but the book has none.

    EXPECT_NE(check_conservation(book, tally), std::nullopt);
}

// --- check_all ----------------------------------------------------------------

TEST(InvariantsTest, CheckAllPassesForAConsistentBook) {
    OrderBook book;
    book.add(Order{book.next_id(), Side::Buy, 10000, 10});
    book.add(Order{book.next_id(), Side::Sell, 10100, 10});

    Tally tally;
    tally.added_buy = 10;
    tally.added_sell = 10;

    EXPECT_EQ(check_all(book, tally), std::nullopt);
}

TEST(InvariantsTest, CheckAllReportsFirstViolation) {
    OrderBook book;
    book.add(Order{book.next_id(), Side::Buy, 10100, 10});
    book.add(Order{book.next_id(), Side::Sell, 10000, 10}); // crossed

    Tally tally; // also wrong, but crossed should be reported first
    EXPECT_NE(check_all(book, tally), std::nullopt);
}

} // namespace
} // namespace orderbook
