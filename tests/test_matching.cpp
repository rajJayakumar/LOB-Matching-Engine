#include <gtest/gtest.h>
#include <ob/order_book.hpp>

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
