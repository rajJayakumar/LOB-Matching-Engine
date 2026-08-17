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

TEST(Edge, ExactLevelExhaustionRemovesLevel) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 50));
    book.add(make_limit(2, ob::Side::Sell, 1000, 50));

    // Buy exactly 100 — exhausts the level
    auto trades = book.add(make_limit(3, ob::Side::Buy, 1000, 100));
    EXPECT_EQ(trades.size(), 2u);
    EXPECT_EQ(book.ask_level_count(), 0u);
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(Edge, ZeroQuantityRejected) {
    ob::OrderBook book;
    auto trades = book.add(make_limit(1, ob::Side::Buy, 1000, 0));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(Edge, InterleavedAddCancelMatch) {
    ob::OrderBook book;

    // Build up some book state
    book.add(make_limit(1, ob::Side::Sell, 1002, 100));
    book.add(make_limit(2, ob::Side::Sell, 1001, 200));
    book.add(make_limit(3, ob::Side::Buy,  999, 150));
    book.add(make_limit(4, ob::Side::Buy,  998, 100));

    // Cancel some
    book.cancel(3);  // remove bid at 999
    EXPECT_EQ(book.best_bid().value(), 998);

    // Match across remaining
    auto trades = book.add(make_limit(5, ob::Side::Buy, 1002, 250));
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].price, 1001);
    EXPECT_EQ(trades[0].quantity, 200u);
    EXPECT_EQ(trades[1].price, 1002);
    EXPECT_EQ(trades[1].quantity, 50u);

    // 50 of sell at 1002 remains
    EXPECT_EQ(book.best_ask().value(), 1002);
    auto asks = book.top_n_asks(1);
    EXPECT_EQ(asks[0].total_qty, 50u);

    // Bid at 998 untouched
    EXPECT_EQ(book.best_bid().value(), 998);
}

TEST(Edge, BookNeverCrossed) {
    ob::OrderBook book;

    // Add bids and asks that don't cross
    book.add(make_limit(1, ob::Side::Buy,  999, 100));
    book.add(make_limit(2, ob::Side::Sell, 1000, 100));

    auto bb = book.best_bid();
    auto ba = book.best_ask();
    ASSERT_TRUE(bb.has_value());
    ASSERT_TRUE(ba.has_value());
    EXPECT_LT(bb.value(), ba.value());
}

TEST(Edge, MultipleAddsAndCancelsLeaveConsistentState) {
    ob::OrderBook book;

    for (ob::OrderId i = 1; i <= 10; ++i) {
        book.add(make_limit(i, ob::Side::Buy, 1000 - i, 100));
        book.add(make_limit(i + 100, ob::Side::Sell, 1000 + i, 100));
    }
    EXPECT_EQ(book.order_count(), 20u);

    // Cancel all bids
    for (ob::OrderId i = 1; i <= 10; ++i) {
        EXPECT_TRUE(book.cancel(i));
    }
    EXPECT_EQ(book.order_count(), 10u);
    EXPECT_EQ(book.bid_level_count(), 0u);
    EXPECT_FALSE(book.best_bid().has_value());

    // Asks untouched
    EXPECT_EQ(book.ask_level_count(), 10u);
}

TEST(Edge, SelfTradeAtSamePrice) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 100));
    // A buy at the resting sell's price should trade (same-price crossing)
    auto trades = book.add(make_limit(2, ob::Side::Buy, 1000, 100));
    EXPECT_EQ(trades.size(), 1u);
    EXPECT_EQ(book.order_count(), 0u);
}
