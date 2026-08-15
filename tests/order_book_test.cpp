#include "orderbook/book.hpp"
#include "orderbook/order.hpp"
#include "orderbook/types.hpp"

#include <gtest/gtest.h>

namespace orderbook {
namespace {

// --- Order ---------------------------------------------------------------

TEST(OrderTest, FieldsAreStoredAsConstructed) {
    Order o{/*order_id=*/1, Side::Buy, /*price=*/10050, /*qty=*/100};

    EXPECT_EQ(o.order_id, 1);
    EXPECT_EQ(o.side, Side::Buy);
    EXPECT_EQ(o.price, 10050);
    EXPECT_EQ(o.qty, 100);
}

// --- Empty book ------------------------------------------------------------

TEST(OrderBookTest, EmptyBookHasNoDepth) {
    OrderBook book;

    auto [bids, asks] = book.depth();

    EXPECT_TRUE(bids.empty());
    EXPECT_TRUE(asks.empty());
}

TEST(OrderBookTest, CancelOnEmptyBookReturnsNullopt) {
    OrderBook book;

    EXPECT_EQ(book.cancel(0), std::nullopt);
}

TEST(OrderBookTest, ExecuteOnEmptyBookProducesNoFills) {
    OrderBook book;

    Order incoming{book.next_id(), Side::Buy, 10000, 50};
    ExecuteResult result = book.execute(incoming);

    EXPECT_TRUE(result.fills.empty());
    EXPECT_EQ(result.remaining_qty, 50);
}

// --- add() -----------------------------------------------------------------

TEST(OrderBookTest, AddSingleBidCreatesOneLevel) {
    OrderBook book;

    Order o{book.next_id(), Side::Buy, 10050, 100};
    book.add(o);

    auto [bids, asks] = book.depth();

    ASSERT_EQ(bids.size(), 1u);
    EXPECT_EQ(bids[0].first, 10050);
    EXPECT_EQ(bids[0].second, 100);
    EXPECT_TRUE(asks.empty());
}

TEST(OrderBookTest, AddTwoOrdersSamePriceAggregatesQty) {
    OrderBook book;

    book.add(Order{book.next_id(), Side::Sell, 10100, 40});
    book.add(Order{book.next_id(), Side::Sell, 10100, 60});

    auto [bids, asks] = book.depth();

    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(asks[0].first, 10100);
    EXPECT_EQ(asks[0].second, 100);
}

TEST(OrderBookTest, BidLevelsAreBestFirstDescending) {
    OrderBook book;

    book.add(Order{book.next_id(), Side::Buy, 9900, 10});
    book.add(Order{book.next_id(), Side::Buy, 10100, 10});
    book.add(Order{book.next_id(), Side::Buy, 10000, 10});

    auto [bids, asks] = book.depth();

    ASSERT_EQ(bids.size(), 3u);
    EXPECT_EQ(bids[0].first, 10100);
    EXPECT_EQ(bids[1].first, 10000);
    EXPECT_EQ(bids[2].first, 9900);
}

TEST(OrderBookTest, AskLevelsAreBestFirstAscending) {
    OrderBook book;

    book.add(Order{book.next_id(), Side::Sell, 10200, 10});
    book.add(Order{book.next_id(), Side::Sell, 10000, 10});
    book.add(Order{book.next_id(), Side::Sell, 10100, 10});

    auto [bids, asks] = book.depth();

    ASSERT_EQ(asks.size(), 3u);
    EXPECT_EQ(asks[0].first, 10000);
    EXPECT_EQ(asks[1].first, 10100);
    EXPECT_EQ(asks[2].first, 10200);
}

// --- cancel() ----------------------------------------------------------

TEST(OrderBookTest, CancelRemovesOrderAndPrunesEmptiedLevel) {
    OrderBook book;

    OrderId id = book.next_id();
    book.add(Order{id, Side::Buy, 10050, 100});

    std::optional<Order> removed = book.cancel(id);

    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(removed->order_id, id);
    EXPECT_EQ(removed->qty, 100);

    auto [bids, asks] = book.depth();
    EXPECT_TRUE(bids.empty());
}

TEST(OrderBookTest, CancelOneOfTwoOrdersAtSameLevelLeavesTheOther) {
    OrderBook book;

    OrderId first = book.next_id();
    book.add(Order{first, Side::Buy, 10050, 40});
    OrderId second = book.next_id();
    book.add(Order{second, Side::Buy, 10050, 60});

    book.cancel(first);

    auto [bids, asks] = book.depth();

    ASSERT_EQ(bids.size(), 1u);
    EXPECT_EQ(bids[0].first, 10050);
    EXPECT_EQ(bids[0].second, 60);
}

TEST(OrderBookTest, CancelUnknownIdReturnsNulloptAndLeavesBookUnchanged) {
    OrderBook book;

    book.add(Order{book.next_id(), Side::Buy, 10050, 100});

    EXPECT_EQ(book.cancel(999), std::nullopt);

    auto [bids, asks] = book.depth();
    ASSERT_EQ(bids.size(), 1u);
    EXPECT_EQ(bids[0].second, 100);
}

// --- execute() -----------------------------------------------------------

TEST(OrderBookTest, ExecuteFullyFillsAgainstRestingOrder) {
    OrderBook book;

    book.add(Order{book.next_id(), Side::Sell, 10050, 100});

    Order incoming{book.next_id(), Side::Buy, 10050, 100};
    ExecuteResult result = book.execute(incoming);

    ASSERT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].price, 10050);
    EXPECT_EQ(result.fills[0].qty, 100);
    EXPECT_EQ(result.remaining_qty, 0);

    auto [bids, asks] = book.depth();
    EXPECT_TRUE(asks.empty());
}

TEST(OrderBookTest, ExecutePartiallyFillsAndLeavesRemainingQty) {
    OrderBook book;

    book.add(Order{book.next_id(), Side::Sell, 10050, 30});

    Order incoming{book.next_id(), Side::Buy, 10050, 100};
    ExecuteResult result = book.execute(incoming);

    ASSERT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].qty, 30);
    EXPECT_EQ(result.remaining_qty, 70);

    auto [bids, asks] = book.depth();
    EXPECT_TRUE(asks.empty());
}

TEST(OrderBookTest, ExecuteWalksMultipleLevels) {
    OrderBook book;

    book.add(Order{book.next_id(), Side::Sell, 10000, 20});
    book.add(Order{book.next_id(), Side::Sell, 10100, 30});

    Order incoming{book.next_id(), Side::Buy, 10200, 40};
    ExecuteResult result = book.execute(incoming);

    ASSERT_EQ(result.fills.size(), 2u);
    EXPECT_EQ(result.fills[0].price, 10000);
    EXPECT_EQ(result.fills[0].qty, 20);
    EXPECT_EQ(result.fills[1].price, 10100);
    EXPECT_EQ(result.fills[1].qty, 20);
    EXPECT_EQ(result.remaining_qty, 0);

    auto [bids, asks] = book.depth();
    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(asks[0].first, 10100);
    EXPECT_EQ(asks[0].second, 10);
}

TEST(OrderBookTest, ExecuteStopsAtLimitPriceProducingNoFills) {
    OrderBook book;

    book.add(Order{book.next_id(), Side::Sell, 10200, 50});

    Order incoming{book.next_id(), Side::Buy, 10000, 50};
    ExecuteResult result = book.execute(incoming);

    EXPECT_TRUE(result.fills.empty());
    EXPECT_EQ(result.remaining_qty, 50);

    auto [bids, asks] = book.depth();
    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(asks[0].second, 50);
}

TEST(OrderBookTest, ExecuteLeavesRestingOrderPartiallyFilledInPlace) {
    OrderBook book;

    OrderId resting_id = book.next_id();
    book.add(Order{resting_id, Side::Sell, 10050, 100});

    Order incoming{book.next_id(), Side::Buy, 10050, 40};
    ExecuteResult result = book.execute(incoming);

    ASSERT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].resting_id, resting_id);
    EXPECT_EQ(result.fills[0].qty, 40);

    auto [bids, asks] = book.depth();
    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(asks[0].second, 60);
}

TEST(OrderBookTest, ExecuteDoesNotMutateCallersOriginalOrder) {
    OrderBook book;

    book.add(Order{book.next_id(), Side::Sell, 10050, 100});

    Order incoming{book.next_id(), Side::Buy, 10050, 40};
    book.execute(incoming);

    // execute() takes its argument by value — the caller's local `incoming`
    // must be unaffected. See SPEC.md §4b (execute() no longer mutates in
    // place, unlike the Python prototype).
    EXPECT_EQ(incoming.qty, 40);
}

// --- depth() -------------------------------------------------------------

TEST(OrderBookTest, DepthLimitsToTopNLevelsPerSide) {
    OrderBook book;

    book.add(Order{book.next_id(), Side::Buy, 9900, 10});
    book.add(Order{book.next_id(), Side::Buy, 10000, 10});
    book.add(Order{book.next_id(), Side::Buy, 10100, 10});

    book.add(Order{book.next_id(), Side::Sell, 10200, 10});
    book.add(Order{book.next_id(), Side::Sell, 10300, 10});

    auto [bids, asks] = book.depth(1);

    ASSERT_EQ(bids.size(), 1u);
    EXPECT_EQ(bids[0].first, 10100);

    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(asks[0].first, 10200);
}

TEST(OrderBookTest, DepthWithNGreaterThanLevelCountReturnsAllLevels) {
    OrderBook book;

    book.add(Order{book.next_id(), Side::Buy, 10000, 10});

    auto [bids, asks] = book.depth(5);

    ASSERT_EQ(bids.size(), 1u);
    EXPECT_TRUE(asks.empty());
}

} // namespace
} // namespace orderbook
