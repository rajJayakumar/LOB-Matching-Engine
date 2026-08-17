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

TEST(BookBuilder, ScriptedMboSequence) {
    using namespace ob::dbn;
    BookBuilder builder;

    // Add bid at price 100
    MboEvent add_bid;
    add_bid.order_id = 1;
    add_bid.price    = 100;
    add_bid.size     = 50;
    add_bid.action   = Action::Add;
    add_bid.side     = MboSide::Bid;
    builder.apply(add_bid);
    EXPECT_EQ(builder.book().best_bid().value(), 100);
    EXPECT_EQ(builder.book().order_count(), 1u);

    // Add ask at price 102
    MboEvent add_ask;
    add_ask.order_id = 2;
    add_ask.price    = 102;
    add_ask.size     = 30;
    add_ask.action   = Action::Add;
    add_ask.side     = MboSide::Ask;
    builder.apply(add_ask);
    EXPECT_EQ(builder.book().best_ask().value(), 102);

    // Cancel (reduce) 20 from the bid
    MboEvent cancel_evt;
    cancel_evt.order_id = 1;
    cancel_evt.size     = 20;
    cancel_evt.action   = Action::Cancel;
    builder.apply(cancel_evt);
    auto bids = builder.book().top_n_bids(1);
    EXPECT_EQ(bids[0].total_qty, 30u);

    // Fill 30 from the ask
    MboEvent fill_evt;
    fill_evt.order_id = 2;
    fill_evt.size     = 30;
    fill_evt.action   = Action::Fill;
    builder.apply(fill_evt);
    EXPECT_FALSE(builder.book().best_ask().has_value());

    // Trade event — should not mutate the book
    MboEvent trade_evt;
    trade_evt.order_id = 1;
    trade_evt.action   = Action::Trade;
    builder.apply(trade_evt);
    EXPECT_EQ(builder.book().order_count(), 1u);

    // Clear
    MboEvent clear_evt;
    clear_evt.action = Action::Clear;
    builder.apply(clear_evt);
    EXPECT_EQ(builder.book().order_count(), 0u);
    EXPECT_FALSE(builder.book().best_bid().has_value());
}

TEST(BookBuilder, ModifyChangesPrice) {
    using namespace ob::dbn;
    BookBuilder builder;

    MboEvent add;
    add.order_id = 1;
    add.price    = 100;
    add.size     = 50;
    add.action   = Action::Add;
    add.side     = MboSide::Bid;
    builder.apply(add);
    EXPECT_EQ(builder.book().best_bid().value(), 100);

    MboEvent modify;
    modify.order_id = 1;
    modify.price    = 101;
    modify.size     = 40;
    modify.action   = Action::Modify;
    builder.apply(modify);
    EXPECT_EQ(builder.book().best_bid().value(), 101);
    auto bids = builder.book().top_n_bids(1);
    EXPECT_EQ(bids[0].total_qty, 40u);
}

TEST(BookBuilder, SkipsUndefPrice) {
    using namespace ob::dbn;
    BookBuilder builder;

    MboEvent add;
    add.order_id = 1;
    add.price    = UNDEF_PRICE;
    add.size     = 50;
    add.action   = Action::Add;
    add.side     = MboSide::Bid;
    builder.apply(add);
    EXPECT_EQ(builder.book().order_count(), 0u);
}
