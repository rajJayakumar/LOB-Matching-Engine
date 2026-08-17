#pragma once

#include <ob/order_book.hpp>
#include <sstream>
#include <string>

namespace ob {

struct InvariantResult {
    bool ok = true;
    std::string message;
};

// Reusable invariant checker — call after every operation.
// Used by fuzz tests (Task 1.12) and ITCH replay coherence (Task 2B.6).
inline InvariantResult check_invariants(const OrderBook& book) {
    InvariantResult r;
    std::ostringstream ss;

    // 1. Book is never crossed
    auto bb = book.best_bid();
    auto ba = book.best_ask();
    if (bb.has_value() && ba.has_value() && bb.value() >= ba.value()) {
        ss << "CROSSED: best_bid=" << bb.value() << " >= best_ask=" << ba.value();
        r.ok = false;
        r.message = ss.str();
        return r;
    }

    // 2. No empty price levels
    auto bids = book.top_n_bids(book.bid_level_count());
    for (const auto& lvl : bids) {
        if (lvl.order_count == 0 || lvl.total_qty == 0) {
            ss << "EMPTY BID LEVEL at price=" << lvl.price;
            r.ok = false;
            r.message = ss.str();
            return r;
        }
    }
    auto asks = book.top_n_asks(book.ask_level_count());
    for (const auto& lvl : asks) {
        if (lvl.order_count == 0 || lvl.total_qty == 0) {
            ss << "EMPTY ASK LEVEL at price=" << lvl.price;
            r.ok = false;
            r.message = ss.str();
            return r;
        }
    }

    // 3. Cancel index matches resting order count
    std::size_t total_resting = 0;
    for (const auto& lvl : bids) total_resting += lvl.order_count;
    for (const auto& lvl : asks) total_resting += lvl.order_count;
    if (book.order_count() != total_resting) {
        ss << "INDEX MISMATCH: index=" << book.order_count()
           << " resting=" << total_resting;
        r.ok = false;
        r.message = ss.str();
        return r;
    }

    return r;
}

// Check share conservation for a single trade sequence
inline InvariantResult check_trade_conservation(
    Qty aggressor_original_qty, Qty aggressor_remaining_qty,
    const std::vector<Trade>& trades)
{
    InvariantResult r;
    Qty total_traded = 0;
    for (const auto& t : trades) {
        total_traded += t.quantity;
    }
    Qty aggressor_filled = aggressor_original_qty - aggressor_remaining_qty;
    if (aggressor_filled != total_traded) {
        std::ostringstream ss;
        ss << "SHARE CONSERVATION: aggressor_filled=" << aggressor_filled
           << " total_traded=" << total_traded;
        r.ok = false;
        r.message = ss.str();
    }
    return r;
}

} // namespace ob
