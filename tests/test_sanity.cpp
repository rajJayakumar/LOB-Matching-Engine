#include <gtest/gtest.h>
#include <ob/order.hpp>
#include <ob/trade.hpp>

TEST(CoreTypes, OrderConstruction) {
    ob::Order o;
    o.order_id = 42;
    o.side     = ob::Side::Sell;
    o.kind     = ob::OrderKind::Limit;
    o.tif      = ob::TimeInForce::GTC;
    o.price    = 100'0000;  // e.g. 1e-4 scale
    o.quantity = 200;
    o.original_quantity = 200;
    o.sequence = 1;

    EXPECT_EQ(o.order_id, 42u);
    EXPECT_EQ(o.side, ob::Side::Sell);
    EXPECT_EQ(o.kind, ob::OrderKind::Limit);
    EXPECT_EQ(o.tif, ob::TimeInForce::GTC);
    EXPECT_EQ(o.price, 100'0000);
    EXPECT_EQ(o.quantity, 200u);
    EXPECT_EQ(o.original_quantity, 200u);
    EXPECT_EQ(o.sequence, 1u);
}

TEST(CoreTypes, TradeConstruction) {
    ob::Trade t;
    t.aggressor_id = 10;
    t.resting_id   = 5;
    t.price        = 585'320'000'000;  // nanodollar scale
    t.quantity     = 100;
    t.sequence     = 7;

    EXPECT_EQ(t.aggressor_id, 10u);
    EXPECT_EQ(t.resting_id, 5u);
    EXPECT_EQ(t.price, 585'320'000'000);
    EXPECT_EQ(t.quantity, 100u);
    EXPECT_EQ(t.sequence, 7u);
}
