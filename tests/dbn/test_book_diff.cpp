#include <gtest/gtest.h>
#include <ob/dbn/loader.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

constexpr int NUM_LEVELS = 10;
constexpr std::uint8_t FLAG_LAST = 0x80;

struct DiffResult {
    bool ok = true;
    std::size_t events_matched = 0;
    std::size_t divergence_event = 0;
    int divergence_level = -1;
    std::string detail;
};

DiffResult run_diff(const std::string& mbo_path, const std::string& mbp_path) {
    DiffResult result;
    auto mbo_events = ob::dbn::load_mbo(mbo_path);
    auto mbp_snaps  = ob::dbn::load_mbp10(mbp_path);

    ob::dbn::BookBuilder builder;
    std::size_t mbp_idx = 0;

    for (std::size_t i = 0; i < mbo_events.size(); ++i) {
        builder.apply(mbo_events[i]);

        bool is_last = (mbo_events[i].flags & FLAG_LAST) != 0;
        if (!is_last || mbp_idx >= mbp_snaps.size()) continue;

        // Skip MBP records whose sequence we've already passed.
        while (mbp_idx < mbp_snaps.size() && mbp_snaps[mbp_idx].sequence < mbo_events[i].sequence) {
            mbp_idx++;
        }
        if (mbp_idx >= mbp_snaps.size()) continue;
        if (mbp_snaps[mbp_idx].sequence != mbo_events[i].sequence) continue;

        const auto& ref = mbp_snaps[mbp_idx];
        auto my_bids = builder.book().top_n_bids(NUM_LEVELS);
        auto my_asks = builder.book().top_n_asks(NUM_LEVELS);

        for (int lvl = 0; lvl < NUM_LEVELS; ++lvl) {
            ob::Price exp_bid = ref.levels[lvl].bid_px;
            ob::Price exp_ask = ref.levels[lvl].ask_px;
            ob::Qty   exp_bsz = ref.levels[lvl].bid_sz;
            ob::Qty   exp_asz = ref.levels[lvl].ask_sz;
            std::uint32_t exp_bct = ref.levels[lvl].bid_ct;
            std::uint32_t exp_act = ref.levels[lvl].ask_ct;

            ob::Price my_bid = (lvl < static_cast<int>(my_bids.size())) ? my_bids[lvl].price : ob::dbn::UNDEF_PRICE;
            ob::Price my_ask = (lvl < static_cast<int>(my_asks.size())) ? my_asks[lvl].price : ob::dbn::UNDEF_PRICE;
            ob::Qty   my_bsz = (lvl < static_cast<int>(my_bids.size())) ? my_bids[lvl].total_qty : 0;
            ob::Qty   my_asz = (lvl < static_cast<int>(my_asks.size())) ? my_asks[lvl].total_qty : 0;
            std::size_t my_bct = (lvl < static_cast<int>(my_bids.size())) ? my_bids[lvl].order_count : 0;
            std::size_t my_act = (lvl < static_cast<int>(my_asks.size())) ? my_asks[lvl].order_count : 0;

            bool ref_bid_undef = (exp_bid == ob::dbn::UNDEF_PRICE);
            bool ref_ask_undef = (exp_ask == ob::dbn::UNDEF_PRICE);
            bool my_bid_undef  = (my_bid == ob::dbn::UNDEF_PRICE);
            bool my_ask_undef  = (my_ask == ob::dbn::UNDEF_PRICE);

            bool bid_ok = (ref_bid_undef && my_bid_undef) ||
                          (!ref_bid_undef && !my_bid_undef &&
                           exp_bid == my_bid && exp_bsz == my_bsz &&
                           exp_bct == static_cast<std::uint32_t>(my_bct));
            bool ask_ok = (ref_ask_undef && my_ask_undef) ||
                          (!ref_ask_undef && !my_ask_undef &&
                           exp_ask == my_ask && exp_asz == my_asz &&
                           exp_act == static_cast<std::uint32_t>(my_act));

            if (!bid_ok || !ask_ok) {
                result.ok = false;
                result.divergence_event = mbp_idx;
                result.divergence_level = lvl;
                std::ostringstream ss;
                ss << "Event " << mbp_idx << " level " << lvl;
                if (!bid_ok) {
                    ss << " BID exp(px=" << exp_bid << " sz=" << exp_bsz << " ct=" << exp_bct
                       << ") got(px=" << my_bid << " sz=" << my_bsz << " ct=" << my_bct << ")";
                }
                if (!ask_ok) {
                    ss << " ASK exp(px=" << exp_ask << " sz=" << exp_asz << " ct=" << exp_act
                       << ") got(px=" << my_ask << " sz=" << my_asz << " ct=" << my_act << ")";
                }
                result.detail = ss.str();
                return result;
            }
        }
        result.events_matched++;
        mbp_idx++;
    }
    return result;
}

} // namespace

TEST(BookDiff, IntcFullSession) {
    const std::string mbo_path = "data/databento/INTC_2025-06-04_mbo.dbn.zst";
    const std::string mbp_path = "data/databento/INTC_2025-06-04_mbp-10.dbn.zst";

    if (!std::filesystem::exists(mbo_path) || !std::filesystem::exists(mbp_path)) {
        std::cout << "SKIPPED: INTC Databento data files not found in data/databento/\n";
        GTEST_SKIP() << "INTC data files absent";
    }

    auto result = run_diff(mbo_path, mbp_path);
    EXPECT_TRUE(result.ok) << result.detail;
    EXPECT_GT(result.events_matched, 100000u)
        << "Expected >100k matches but got " << result.events_matched;
    std::cout << "INTC: " << result.events_matched
              << " events matched to " << NUM_LEVELS << " levels\n";
}

TEST(BookDiff, AaplFullSession) {
    const std::string mbo_path = "data/databento/AAPL_2025-06-04_mbo.dbn.zst";
    const std::string mbp_path = "data/databento/AAPL_2025-06-04_mbp-10.dbn.zst";

    if (!std::filesystem::exists(mbo_path) || !std::filesystem::exists(mbp_path)) {
        std::cout << "SKIPPED: AAPL Databento data files not found in data/databento/\n";
        GTEST_SKIP() << "AAPL data files absent";
    }

    auto result = run_diff(mbo_path, mbp_path);
    EXPECT_TRUE(result.ok) << result.detail;
    EXPECT_GT(result.events_matched, 100000u)
        << "Expected >100k matches but got " << result.events_matched;
    std::cout << "AAPL: " << result.events_matched
              << " events matched to " << NUM_LEVELS << " levels\n";
}
