#include <ob/dbn/loader.hpp>

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

constexpr int NUM_LEVELS = 10;
constexpr std::uint8_t FLAG_LAST = 0x80;  // FlagSet::kLast

bool compare_top10(const ob::OrderBook& book,
                   const ob::dbn::Mbp10Snapshot& ref,
                   std::uint64_t seq, std::size_t event_idx) {
    auto my_bids = book.top_n_bids(NUM_LEVELS);
    auto my_asks = book.top_n_asks(NUM_LEVELS);

    for (int i = 0; i < NUM_LEVELS; ++i) {
        ob::Price exp_bid = ref.levels[i].bid_px;
        ob::Price exp_ask = ref.levels[i].ask_px;
        ob::Qty   exp_bsz = ref.levels[i].bid_sz;
        ob::Qty   exp_asz = ref.levels[i].ask_sz;
        std::uint32_t exp_bct = ref.levels[i].bid_ct;
        std::uint32_t exp_act = ref.levels[i].ask_ct;

        ob::Price my_bid = (i < static_cast<int>(my_bids.size())) ? my_bids[i].price : ob::dbn::UNDEF_PRICE;
        ob::Price my_ask = (i < static_cast<int>(my_asks.size())) ? my_asks[i].price : ob::dbn::UNDEF_PRICE;
        ob::Qty   my_bsz = (i < static_cast<int>(my_bids.size())) ? my_bids[i].total_qty : 0;
        ob::Qty   my_asz = (i < static_cast<int>(my_asks.size())) ? my_asks[i].total_qty : 0;
        std::size_t my_bct = (i < static_cast<int>(my_bids.size())) ? my_bids[i].order_count : 0;
        std::size_t my_act = (i < static_cast<int>(my_asks.size())) ? my_asks[i].order_count : 0;

        bool ref_bid_undef = (exp_bid == ob::dbn::UNDEF_PRICE);
        bool ref_ask_undef = (exp_ask == ob::dbn::UNDEF_PRICE);
        bool my_bid_undef  = (my_bid == ob::dbn::UNDEF_PRICE);
        bool my_ask_undef  = (my_ask == ob::dbn::UNDEF_PRICE);

        bool bid_match = (ref_bid_undef && my_bid_undef) ||
                         (!ref_bid_undef && !my_bid_undef &&
                          exp_bid == my_bid && exp_bsz == my_bsz &&
                          exp_bct == static_cast<std::uint32_t>(my_bct));
        bool ask_match = (ref_ask_undef && my_ask_undef) ||
                         (!ref_ask_undef && !my_ask_undef &&
                          exp_ask == my_ask && exp_asz == my_asz &&
                          exp_act == static_cast<std::uint32_t>(my_act));

        if (!bid_match || !ask_match) {
            std::cout << "DIVERGENCE at event " << event_idx
                      << " seq=" << seq << " level=" << i << "\n";
            if (!bid_match) {
                std::cout << "  BID expected: px=" << exp_bid << " sz=" << exp_bsz
                          << " ct=" << exp_bct << "\n";
                std::cout << "  BID actual:   px=" << my_bid << " sz=" << my_bsz
                          << " ct=" << my_bct << "\n";
            }
            if (!ask_match) {
                std::cout << "  ASK expected: px=" << exp_ask << " sz=" << exp_asz
                          << " ct=" << exp_act << "\n";
                std::cout << "  ASK actual:   px=" << my_ask << " sz=" << my_asz
                          << " ct=" << my_act << "\n";
            }
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: dbn_diff <mbo.dbn.zst> <mbp10.dbn.zst>\n";
        return 1;
    }

    std::string mbo_path  = argv[1];
    std::string mbp_path  = argv[2];

    std::cout << "Loading MBO from " << mbo_path << " ...\n";
    auto mbo_events = ob::dbn::load_mbo(mbo_path);
    std::cout << "  " << mbo_events.size() << " MBO records\n";

    std::cout << "Loading MBP-10 from " << mbp_path << " ...\n";
    auto mbp_snaps = ob::dbn::load_mbp10(mbp_path);
    std::cout << "  " << mbp_snaps.size() << " MBP-10 records\n";

    // Databento prices are nanodollars (1e-9). Sub-penny midpoints at $0.005;
    // use $0.001 (1M nanodollars) tick for safety.
    ob::dbn::BookBuilder builder(1'000'000, 65536);

    std::size_t mbp_idx = 0;
    std::size_t events_matched = 0;

    for (std::size_t i = 0; i < mbo_events.size(); ++i) {
        const auto& evt = mbo_events[i];
        builder.apply(evt);

        bool is_last = (evt.flags & FLAG_LAST) != 0;
        // T (trade) events don't produce MBP-10 records — skip them.
        if (!is_last || evt.action == ob::dbn::Action::Trade || mbp_idx >= mbp_snaps.size()) continue;

        // Skip any MBP records whose sequence we've already passed.
        // Some MBP snapshots correspond to non-FLAG_LAST MBO actions
        // and cannot be matched from our FLAG_LAST-only view.
        while (mbp_idx < mbp_snaps.size() && mbp_snaps[mbp_idx].sequence < evt.sequence) {
            mbp_idx++;
        }
        if (mbp_idx >= mbp_snaps.size()) continue;

        // Compare when sequences match
        if (mbp_snaps[mbp_idx].sequence == evt.sequence) {
            if (!compare_top10(builder.book(), mbp_snaps[mbp_idx], evt.sequence, mbp_idx)) {
                std::cout << "First divergence at MBP-10 event " << mbp_idx
                          << " (MBO index " << i << ")\n";
                return 1;
            }
            events_matched++;
            mbp_idx++;
        }
    }

    std::cout << "\nCLEAN MATCH: " << events_matched
              << " events matched to " << NUM_LEVELS << " levels.\n";
    return 0;
}
