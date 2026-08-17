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
