#include <gtest/gtest.h>
#include <ob/order_book.hpp>

namespace {
ob::Order make_limit(ob::OrderId id, ob::Side side, ob::Price price, ob::Qty qty) {
    ob::Order o;
    o.order_id = id;
    o.side     = side;
    o.kind     = ob::OrderKind::Limit;
    o.tif      = ob::TimeInForce::GTC;
    o.price    = price;
    o.quantity = qty;
    return o;
}
} // namespace

TEST(Cancel, CancelRestingOrder) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Buy, 1000, 100));
    book.add(make_limit(2, ob::Side::Buy, 1000, 200));
    EXPECT_EQ(book.order_count(), 2u);

    EXPECT_TRUE(book.cancel(1));
    EXPECT_EQ(book.order_count(), 1u);

    auto bids = book.top_n_bids(1);
    EXPECT_EQ(bids[0].total_qty, 200u);
    EXPECT_EQ(bids[0].order_count, 1u);
}

TEST(Cancel, CancelEmptiesLevel) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 100));
    EXPECT_EQ(book.ask_level_count(), 1u);

    EXPECT_TRUE(book.cancel(1));
    EXPECT_EQ(book.ask_level_count(), 0u);
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(Cancel, CancelUnknownId) {
    ob::OrderBook book;
    EXPECT_FALSE(book.cancel(999));
}

TEST(Cancel, CancelAlreadyFilledId) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 100));
    book.add(make_limit(2, ob::Side::Buy, 1000, 100));  // fills order 1

    EXPECT_FALSE(book.cancel(1));
}

TEST(Cancel, BestBidUpdatesAfterCancel) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Buy, 1001, 100));
    book.add(make_limit(2, ob::Side::Buy, 1000, 100));
    EXPECT_EQ(book.best_bid().value(), 1001);

    EXPECT_TRUE(book.cancel(1));
    EXPECT_EQ(book.best_bid().value(), 1000);
}

TEST(Reduce, ReduceInPlace) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 100));

    EXPECT_TRUE(book.reduce(1, 30));
    auto asks = book.top_n_asks(1);
    EXPECT_EQ(asks[0].total_qty, 70u);
    EXPECT_EQ(book.order_count(), 1u);
}

TEST(Reduce, ReduceToZeroRemoves) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 100));

    EXPECT_TRUE(book.reduce(1, 100));
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(Reduce, ReduceMoreThanRemainingRemoves) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 50));

    EXPECT_TRUE(book.reduce(1, 200));
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(Modify, PriceChangeResetsTimePriority) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 100));
    book.add(make_limit(2, ob::Side::Sell, 1000, 100));

    // Modify order 1 to a different price — loses time priority at old level
    book.modify(1, 1001, 100);

    // Order 2 should now be at 1000 (best ask), order 1 at 1001
    EXPECT_EQ(book.best_ask().value(), 1000);
    EXPECT_EQ(book.ask_level_count(), 2u);

    // Match at 1000 — should get order 2 (not 1)
    auto trades = book.add(make_limit(3, ob::Side::Buy, 1000, 100));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].resting_id, 2u);
}

TEST(Modify, SamePriceKeepsPriority) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 100));
    book.add(make_limit(2, ob::Side::Sell, 1000, 100));

    // Modify order 1's quantity at the same price — keeps time priority
    book.modify(1, 1000, 50);
    auto asks = book.top_n_asks(1);
    EXPECT_EQ(asks[0].total_qty, 150u);

    // Match — should still get order 1 first (it has time priority)
    auto trades = book.add(make_limit(3, ob::Side::Buy, 1000, 50));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].resting_id, 1u);
    EXPECT_EQ(trades[0].quantity, 50u);
}

TEST(Modify, UnknownIdReturnsEmpty) {
    ob::OrderBook book;
    auto trades = book.modify(999, 1000, 100);
    EXPECT_TRUE(trades.empty());
}
