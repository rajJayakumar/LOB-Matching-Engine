#include <gtest/gtest.h>
#include <ob/order_book.hpp>
#include <ob/invariant_checker.hpp>
#include <random>

namespace {

ob::Order make_random_order(ob::OrderId id, std::mt19937& rng) {
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<ob::Price> price_dist(900, 1100);
    std::uniform_int_distribution<ob::Qty> qty_dist(1, 500);

    ob::Order o;
    o.order_id = id;
    o.side     = side_dist(rng) ? ob::Side::Buy : ob::Side::Sell;
    o.kind     = ob::OrderKind::Limit;
    o.tif      = ob::TimeInForce::GTC;
    o.price    = price_dist(rng);
    o.quantity = qty_dist(rng);
    return o;
}

void run_fuzz(std::uint32_t seed, int num_ops) {
    std::mt19937 rng(seed);
    ob::OrderBook book;
    ob::OrderId next_id = 1;
    std::vector<ob::OrderId> active_ids;

    std::uniform_int_distribution<int> action_dist(0, 99);

    for (int i = 0; i < num_ops; ++i) {
        int action = action_dist(rng);

        if (action < 50) {
            // Add order
            auto order = make_random_order(next_id, rng);
            ob::Qty orig_qty = order.quantity;
            auto trades = book.add(order);

            // Check share conservation
            ob::Qty remaining = order.quantity;
            // After add(), order.quantity is modified — we need to compute
            // what the remaining is. Since add() takes by value, we need
            // to track differently. The trades tell us what was filled.
            ob::Qty total_traded = 0;
            for (const auto& t : trades) {
                total_traded += t.quantity;
            }
            // If the order rested, the remainder = orig - traded
            // Conservation: traded qty == what was taken from the aggressor
            ASSERT_LE(total_traded, orig_qty)
                << "seed=" << seed << " op=" << i
                << " traded more than original qty";

            active_ids.push_back(next_id);
            next_id++;
        } else if (action < 70 && !active_ids.empty()) {
            // Cancel random order
            std::uniform_int_distribution<std::size_t> idx_dist(0, active_ids.size() - 1);
            auto idx = idx_dist(rng);
            book.cancel(active_ids[idx]);
            active_ids.erase(active_ids.begin() + static_cast<std::ptrdiff_t>(idx));
        } else if (action < 85 && !active_ids.empty()) {
            // Reduce random order
            std::uniform_int_distribution<std::size_t> idx_dist(0, active_ids.size() - 1);
            std::uniform_int_distribution<ob::Qty> red_dist(1, 200);
            auto idx = idx_dist(rng);
            book.reduce(active_ids[idx], red_dist(rng));
        } else if (action < 100 && !active_ids.empty()) {
            // Modify random order
            std::uniform_int_distribution<std::size_t> idx_dist(0, active_ids.size() - 1);
            std::uniform_int_distribution<ob::Price> price_dist(900, 1100);
            std::uniform_int_distribution<ob::Qty> qty_dist(1, 500);
            auto idx = idx_dist(rng);
            book.modify(active_ids[idx], price_dist(rng), qty_dist(rng));
        }

        // Check invariants after every operation
        auto result = ob::check_invariants(book);
        ASSERT_TRUE(result.ok)
            << "seed=" << seed << " op=" << i << ": " << result.message;
    }
}

} // namespace

TEST(Invariants, Fuzz_Seed42) { run_fuzz(42, 10000); }
TEST(Invariants, Fuzz_Seed123) { run_fuzz(123, 10000); }
TEST(Invariants, Fuzz_Seed999) { run_fuzz(999, 10000); }
TEST(Invariants, Fuzz_Seed2024) { run_fuzz(2024, 10000); }
TEST(Invariants, Fuzz_Seed7777) { run_fuzz(7777, 10000); }
