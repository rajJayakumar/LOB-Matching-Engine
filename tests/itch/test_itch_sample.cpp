#include <gtest/gtest.h>
#include <ob/itch/parser.hpp>

#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

using namespace ob::itch;

namespace {

const char* ITCH_PATH = "data/itch/08302019.NASDAQ_ITCH50";

struct SanityHandler : Handler {
    std::size_t sys_count = 0;
    std::size_t dir_count = 0;
    std::size_t add_count = 0;
    std::size_t add_mpid_count = 0;
    std::size_t exec_count = 0;
    std::size_t exec_wp_count = 0;
    std::size_t cancel_count = 0;
    std::size_t delete_count = 0;
    std::size_t replace_count = 0;

    // Track active order IDs (added and not yet deleted/fully cancelled)
    std::unordered_set<std::uint64_t> active_orders;
    std::size_t ref_miss = 0;  // References to unknown order IDs

    // Price/size sanity
    std::size_t bad_price = 0;
    std::size_t bad_size = 0;

    void on_system_event(const SystemEvent&) override { ++sys_count; }

    void on_stock_directory(const StockDirectory&) override { ++dir_count; }

    void on_add_order(const AddOrder& m) override {
        ++add_count;
        active_orders.insert(m.order_ref);
        if (m.price <= 0) ++bad_price;
        if (m.shares == 0) ++bad_size;
    }

    void on_add_order_mpid(const AddOrderMPID& m) override {
        ++add_mpid_count;
        active_orders.insert(m.order_ref);
        if (m.price <= 0) ++bad_price;
        if (m.shares == 0) ++bad_size;
    }

    void on_order_executed(const OrderExecuted& m) override {
        ++exec_count;
        if (!active_orders.count(m.order_ref)) ++ref_miss;
    }

    void on_order_executed_wp(const OrderExecutedWithPrice& m) override {
        ++exec_wp_count;
        if (!active_orders.count(m.order_ref)) ++ref_miss;
    }

    void on_order_cancel(const OrderCancel& m) override {
        ++cancel_count;
        if (!active_orders.count(m.order_ref)) ++ref_miss;
    }

    void on_order_delete(const OrderDelete& m) override {
        ++delete_count;
        if (!active_orders.count(m.order_ref)) ++ref_miss;
        active_orders.erase(m.order_ref);
    }

    void on_order_replace(const OrderReplace& m) override {
        ++replace_count;
        if (!active_orders.count(m.original_order_ref)) ++ref_miss;
        active_orders.erase(m.original_order_ref);
        active_orders.insert(m.new_order_ref);
        if (m.price <= 0) ++bad_price;
        if (m.shares == 0) ++bad_size;
    }
};

} // namespace

TEST(ItchSample, StructuralSanity) {
    if (!std::filesystem::exists(ITCH_PATH)) {
        std::cout << "SKIPPED: " << ITCH_PATH << " not found\n";
        GTEST_SKIP() << "ITCH sample absent";
    }

    SanityHandler h;
    auto total = parse_file(ITCH_PATH, h);

    std::cout << "Total messages:  " << total << "\n"
              << "  System events: " << h.sys_count << "\n"
              << "  Directories:   " << h.dir_count << "\n"
              << "  Add (A):       " << h.add_count << "\n"
              << "  Add MPID (F):  " << h.add_mpid_count << "\n"
              << "  Executed (E):  " << h.exec_count << "\n"
              << "  Exec w/px (C): " << h.exec_wp_count << "\n"
              << "  Cancel (X):    " << h.cancel_count << "\n"
              << "  Delete (D):    " << h.delete_count << "\n"
              << "  Replace (U):   " << h.replace_count << "\n"
              << "  Ref misses:    " << h.ref_miss << "\n"
              << "  Bad prices:    " << h.bad_price << "\n"
              << "  Bad sizes:     " << h.bad_size << "\n";

    // Structural assertions
    EXPECT_GT(total, 100000u) << "Expected millions of messages";
    EXPECT_GT(h.sys_count, 0u);
    EXPECT_GT(h.dir_count, 0u);
    EXPECT_GT(h.add_count, 0u);
    EXPECT_GT(h.exec_count, 0u);
    EXPECT_GT(h.cancel_count, 0u);
    EXPECT_GT(h.delete_count, 0u);
    EXPECT_GT(h.replace_count, 0u);

    // All E/C/X/D/U references should resolve to previously-added orders
    EXPECT_EQ(h.ref_miss, 0u) << "Some order references were unresolved";

    // Price and size sanity
    EXPECT_EQ(h.bad_price, 0u) << "Some prices were out of range";
    EXPECT_EQ(h.bad_size, 0u) << "Some sizes were zero";
}
