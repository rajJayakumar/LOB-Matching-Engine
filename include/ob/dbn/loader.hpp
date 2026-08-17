#pragma once

#include <ob/order.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ob::dbn {

// Databento prices are 1e-9 fixed-point (nanodollars).
// UNDEF_PRICE = INT64_MAX is the null sentinel.
constexpr Price UNDEF_PRICE = INT64_MAX;

enum class Action : char {
    Add     = 'A',
    Cancel  = 'C',
    Modify  = 'M',
    Clear   = 'R',
    Trade   = 'T',
    Fill    = 'F',
    None    = 'N',
};

enum class MboSide : char {
    Bid  = 'B',
    Ask  = 'A',
    None = 'N',
};

struct MboEvent {
    std::uint64_t order_id = 0;
    Price         price    = 0;    // 1e-9 fixed-point
    Qty           size     = 0;
    Action        action   = Action::None;
    MboSide       side     = MboSide::None;
    std::uint64_t sequence = 0;
    std::uint64_t ts_event = 0;
    std::uint8_t  flags    = 0;
};

struct Mbp10Level {
    Price       bid_px = UNDEF_PRICE;
    Price       ask_px = UNDEF_PRICE;
    Qty         bid_sz = 0;
    Qty         ask_sz = 0;
    std::uint32_t bid_ct = 0;
    std::uint32_t ask_ct = 0;
};

struct Mbp10Snapshot {
    std::uint64_t ts_event = 0;
    std::uint64_t sequence = 0;
    std::uint8_t  flags    = 0;
    Mbp10Level    levels[10];
};

// Load all MBO events from a DBN file.
std::vector<MboEvent> load_mbo(const std::string& path);

// Load all MBP-10 snapshots from a DBN file.
std::vector<Mbp10Snapshot> load_mbp10(const std::string& path);

} // namespace ob::dbn
