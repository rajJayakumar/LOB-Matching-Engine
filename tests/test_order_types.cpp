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

ob::Order make_market(ob::OrderId id, ob::Side side, ob::Qty qty) {
    ob::Order o;
    o.order_id = id;
    o.side     = side;
    o.kind     = ob::OrderKind::Market;
    o.tif      = ob::TimeInForce::GTC;
    o.price    = 0;
    o.quantity = qty;
    return o;
}
ob::Order make_fok(ob::OrderId id, ob::Side side, ob::Price price, ob::Qty qty) {
    ob::Order o;
    o.order_id = id;
    o.side     = side;
    o.kind     = ob::OrderKind::Limit;
    o.tif      = ob::TimeInForce::FOK;
    o.price    = price;
    o.quantity = qty;
    return o;
}

ob::Order make_ioc(ob::OrderId id, ob::Side side, ob::Price price, ob::Qty qty) {
    ob::Order o;
    o.order_id = id;
    o.side     = side;
    o.kind     = ob::OrderKind::Limit;
    o.tif      = ob::TimeInForce::IOC;
    o.price    = price;
    o.quantity = qty;
    return o;
}
} // namespace

TEST(Market, FullFill) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 100));
    book.add(make_limit(2, ob::Side::Sell, 1001, 200));

    auto trades = book.add(make_market(3, ob::Side::Buy, 250));
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].price, 1000);
    EXPECT_EQ(trades[0].quantity, 100u);
    EXPECT_EQ(trades[1].price, 1001);
    EXPECT_EQ(trades[1].quantity, 150u);

    // 50 remains at 1001
    EXPECT_EQ(book.best_ask().value(), 1001);
}

TEST(Market, PartialFillEmptiesBook) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 50));

    auto trades = book.add(make_market(2, ob::Side::Buy, 200));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 50u);

    // Book empty, remainder discarded (market never rests)
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(Market, IntoEmptyBook) {
    ob::OrderBook book;
    auto trades = book.add(make_market(1, ob::Side::Buy, 100));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(IOC, PartialFillRemainderCancelled) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 50));

    // IOC buy 100@1000 — fills 50, remainder discarded (not rested)
    auto trades = book.add(make_ioc(2, ob::Side::Buy, 1000, 100));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 50u);

    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(IOC, FullFill) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 100));

    auto trades = book.add(make_ioc(2, ob::Side::Buy, 1000, 100));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 100u);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(IOC, NoCross) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 100));

    // IOC buy at 999 — doesn't cross, nothing fills, nothing rests
    auto trades = book.add(make_ioc(2, ob::Side::Buy, 999, 100));
    EXPECT_TRUE(trades.empty());
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.order_count(), 1u);  // only the original sell
}

TEST(FOK, FullFillExecutes) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 50));
    book.add(make_limit(2, ob::Side::Sell, 1001, 50));

    // FOK buy 100@1001 — enough liquidity, should execute
    auto trades = book.add(make_fok(3, ob::Side::Buy, 1001, 100));
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].quantity, 50u);
    EXPECT_EQ(trades[1].quantity, 50u);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(FOK, InsufficientLiquidityRejects) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 50));

    // FOK buy 100@1000 — only 50 available, reject entirely
    auto trades = book.add(make_fok(2, ob::Side::Buy, 1000, 100));
    EXPECT_TRUE(trades.empty());

    // Book untouched
    EXPECT_EQ(book.order_count(), 1u);
    EXPECT_EQ(book.best_ask().value(), 1000);
    auto asks = book.top_n_asks(1);
    EXPECT_EQ(asks[0].total_qty, 50u);
}

TEST(FOK, NoCrossRejects) {
    ob::OrderBook book;
    book.add(make_limit(1, ob::Side::Sell, 1000, 100));

    // FOK buy at 999 — doesn't cross
    auto trades = book.add(make_fok(2, ob::Side::Buy, 999, 100));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.order_count(), 1u);
}
