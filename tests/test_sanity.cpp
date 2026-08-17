#include <gtest/gtest.h>
#include <ob/order.hpp>

TEST(Sanity, TypeAliases) {
    ob::Price   p = 100;
    ob::Qty     q = 50;
    ob::OrderId id = 1;
    EXPECT_EQ(p, 100);
    EXPECT_EQ(q, 50u);
    EXPECT_EQ(id, 1u);
}
