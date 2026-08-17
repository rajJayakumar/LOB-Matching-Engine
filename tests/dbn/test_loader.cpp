#include <gtest/gtest.h>
#include <ob/dbn/loader.hpp>

#include <filesystem>
#include <iostream>

namespace {

const char* MBO_PATH  = "data/databento/INTC_2025-06-04_mbo.dbn.zst";
const char* MBP_PATH  = "data/databento/INTC_2025-06-04_mbp-10.dbn.zst";

bool file_exists(const char* path) {
    return std::filesystem::exists(path);
}

} // namespace

TEST(DbnLoader, LoadMboSkipsIfAbsent) {
    if (!file_exists(MBO_PATH)) {
        std::cout << "SKIPPED: " << MBO_PATH << " not found\n";
        GTEST_SKIP() << "MBO data file absent";
    }

    auto events = ob::dbn::load_mbo(MBO_PATH);
    EXPECT_GT(events.size(), 0u);

    // Verify first event has sensible fields
    const auto& e = events[0];
    EXPECT_NE(e.order_id, 0u);
    EXPECT_NE(e.price, ob::dbn::UNDEF_PRICE);
    EXPECT_GT(e.size, 0u);
    EXPECT_NE(e.action, ob::dbn::Action::None);
}

TEST(DbnLoader, LoadMbp10SkipsIfAbsent) {
    if (!file_exists(MBP_PATH)) {
        std::cout << "SKIPPED: " << MBP_PATH << " not found\n";
        GTEST_SKIP() << "MBP-10 data file absent";
    }

    auto snaps = ob::dbn::load_mbp10(MBP_PATH);
    EXPECT_GT(snaps.size(), 0u);

    // Check that at least the first snapshot has defined prices at level 0
    const auto& s = snaps[0];
    EXPECT_NE(s.levels[0].bid_px, ob::dbn::UNDEF_PRICE);
    EXPECT_NE(s.levels[0].ask_px, ob::dbn::UNDEF_PRICE);
}

TEST(DbnLoader, UndefPriceGuard) {
    EXPECT_EQ(ob::dbn::UNDEF_PRICE, INT64_MAX);
}
