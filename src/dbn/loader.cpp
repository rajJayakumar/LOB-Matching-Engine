#include <ob/dbn/loader.hpp>

#include <databento/dbn_file_store.hpp>
#include <databento/enums.hpp>
#include <databento/record.hpp>

namespace ob::dbn {

namespace {

Action to_action(databento::Action a) {
    switch (a) {
        case databento::Action::Add:    return Action::Add;
        case databento::Action::Cancel: return Action::Cancel;
        case databento::Action::Modify: return Action::Modify;
        case databento::Action::Clear:  return Action::Clear;
        case databento::Action::Trade:  return Action::Trade;
        case databento::Action::Fill:   return Action::Fill;
        default:                        return Action::None;
    }
}

MboSide to_side(databento::Side s) {
    switch (s) {
        case databento::Side::Bid: return MboSide::Bid;
        case databento::Side::Ask: return MboSide::Ask;
        default:                   return MboSide::None;
    }
}

} // namespace

std::vector<MboEvent> load_mbo(const std::string& path) {
    std::vector<MboEvent> events;
    databento::DbnFileStore store(path);

    while (const auto* rec = store.NextRecord()) {
        if (!rec->Holds<databento::MboMsg>()) continue;
        const auto& msg = rec->Get<databento::MboMsg>();

        MboEvent e;
        e.order_id = msg.order_id;
        e.price    = msg.price;
        e.size     = msg.size;
        e.action   = to_action(msg.action);
        e.side     = to_side(msg.side);
        e.sequence = msg.sequence;
        e.ts_event = msg.hd.ts_event.time_since_epoch().count();
        e.flags    = static_cast<std::uint8_t>(msg.flags);
        events.push_back(e);
    }
    return events;
}

std::vector<Mbp10Snapshot> load_mbp10(const std::string& path) {
    std::vector<Mbp10Snapshot> snaps;
    databento::DbnFileStore store(path);

    while (const auto* rec = store.NextRecord()) {
        if (!rec->Holds<databento::Mbp10Msg>()) continue;
        const auto& msg = rec->Get<databento::Mbp10Msg>();

        Mbp10Snapshot s;
        s.ts_event = msg.hd.ts_event.time_since_epoch().count();
        s.sequence = msg.sequence;
        s.flags    = static_cast<std::uint8_t>(msg.flags);

        for (int i = 0; i < 10; ++i) {
            s.levels[i].bid_px = msg.levels[i].bid_px;
            s.levels[i].ask_px = msg.levels[i].ask_px;
            s.levels[i].bid_sz = msg.levels[i].bid_sz;
            s.levels[i].ask_sz = msg.levels[i].ask_sz;
            s.levels[i].bid_ct = msg.levels[i].bid_ct;
            s.levels[i].ask_ct = msg.levels[i].ask_ct;
        }
        snaps.push_back(s);
    }
    return snaps;
}

} // namespace ob::dbn
