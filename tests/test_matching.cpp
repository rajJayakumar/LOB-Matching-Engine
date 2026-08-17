#include <gtest/gtest.h>
#include <ob/order_book.hpp>

namespace {
ob::Order make_order(ob::OrderId id, ob::Side side, ob::Price price, ob::Qty qty) {
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

TEST(OrderBook, EmptyBookNoBestPrices) {
    ob::OrderBook book;
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.bid_level_count(), 0u);
    EXPECT_EQ(book.ask_level_count(), 0u);
    EXPECT_EQ(book.order_count(), 0u);

    auto bids = book.top_n_bids(10);
    auto asks = book.top_n_asks(10);
    EXPECT_TRUE(bids.empty());
    EXPECT_TRUE(asks.empty());
}

TEST(Matching, SingleCrossingTrade) {
    ob::OrderBook book;

    // Add a sell limit at 1000
    auto trades1 = book.add(make_order(1, ob::Side::Sell, 1000, 100));
    EXPECT_TRUE(trades1.empty());
    EXPECT_EQ(book.best_ask().value(), 1000);
    EXPECT_EQ(book.order_count(), 1u);

    // Add a buy limit at 1000 — should cross
    auto trades2 = book.add(make_order(2, ob::Side::Buy, 1000, 100));
    ASSERT_EQ(trades2.size(), 1u);
    EXPECT_EQ(trades2[0].aggressor_id, 2u);
    EXPECT_EQ(trades2[0].resting_id, 1u);
    EXPECT_EQ(trades2[0].price, 1000);
    EXPECT_EQ(trades2[0].quantity, 100u);

    // Book should be empty
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(Resting, BuyBelowBestAskRests) {
    ob::OrderBook book;
    book.add(make_order(1, ob::Side::Sell, 1000, 100));
    // Buy below best ask — should rest
    auto trades = book.add(make_order(2, ob::Side::Buy, 999, 50));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.best_bid().value(), 999);
    EXPECT_EQ(book.best_ask().value(), 1000);
    EXPECT_EQ(book.order_count(), 2u);
}

TEST(Resting, SellAboveBestBidRests) {
    ob::OrderBook book;
    book.add(make_order(1, ob::Side::Buy, 999, 100));
    // Sell above best bid — should rest
    auto trades = book.add(make_order(2, ob::Side::Sell, 1000, 50));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.best_bid().value(), 999);
    EXPECT_EQ(book.best_ask().value(), 1000);
    EXPECT_EQ(book.order_count(), 2u);
}

TEST(Resting, FIFOTimePriorityAtSamePrice) {
    ob::OrderBook book;
    // Two sells at same price — first added should be matched first
    book.add(make_order(1, ob::Side::Sell, 1000, 100));
    book.add(make_order(2, ob::Side::Sell, 1000, 100));
    EXPECT_EQ(book.ask_level_count(), 1u);
    auto asks = book.top_n_asks(1);
    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(asks[0].total_qty, 200u);
    EXPECT_EQ(asks[0].order_count, 2u);

    // Buy 100 — should match order 1 (first in FIFO)
    auto trades = book.add(make_order(3, ob::Side::Buy, 1000, 100));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].resting_id, 1u);
    EXPECT_EQ(book.order_count(), 1u);
}

TEST(Matching, PartialFillsAcrossMultipleLevels) {
    ob::OrderBook book;
    // Asks: 300@10.01, 200@10.02, 500@10.03
    book.add(make_order(1, ob::Side::Sell, 1001, 300));
    book.add(make_order(2, ob::Side::Sell, 1002, 200));
    book.add(make_order(3, ob::Side::Sell, 1003, 500));
    EXPECT_EQ(book.ask_level_count(), 3u);

    // Buy 600 @ 10.03 — crosses all three levels, fills 300+200+100
    auto trades = book.add(make_order(4, ob::Side::Buy, 1003, 600));
    ASSERT_EQ(trades.size(), 3u);

    // First fill: 300 @ 10.01 (best ask)
    EXPECT_EQ(trades[0].resting_id, 1u);
    EXPECT_EQ(trades[0].price, 1001);
    EXPECT_EQ(trades[0].quantity, 300u);

    // Second fill: 200 @ 10.02
    EXPECT_EQ(trades[1].resting_id, 2u);
    EXPECT_EQ(trades[1].price, 1002);
    EXPECT_EQ(trades[1].quantity, 200u);

    // Third fill: 100 @ 10.03 (partial)
    EXPECT_EQ(trades[2].resting_id, 3u);
    EXPECT_EQ(trades[2].price, 1003);
    EXPECT_EQ(trades[2].quantity, 100u);

    // 400 remains resting at 10.03
    EXPECT_EQ(book.ask_level_count(), 1u);
    EXPECT_EQ(book.best_ask().value(), 1003);
    auto asks = book.top_n_asks(1);
    EXPECT_EQ(asks[0].total_qty, 400u);

    // Aggressor fully filled — should NOT rest
    EXPECT_FALSE(book.best_bid().has_value());
}
