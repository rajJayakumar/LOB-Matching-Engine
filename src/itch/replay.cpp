#include <ob/itch/replay.hpp>

namespace ob::itch {

ReplayHandler::ReplayHandler(const std::string& target_symbol,
                             Price tick_size, std::size_t band_size)
    : target_symbol_(target_symbol)
    , book_(tick_size, band_size) {}

void ReplayHandler::set_snapshot_callback(SnapshotCallback cb, std::size_t interval) {
    snapshot_cb_ = std::move(cb);
    snapshot_interval_ = interval;
}

bool ReplayHandler::is_target(std::uint16_t stock_locate) const {
    return target_found_ && stock_locate == target_locate_;
}

void ReplayHandler::maybe_snapshot(std::uint64_t ts) {
    if (snapshot_cb_ && snapshot_interval_ > 0 &&
        events_applied_ % snapshot_interval_ == 0) {
        snapshot_cb_(ts, book_);
    }
}

void ReplayHandler::on_stock_directory(const StockDirectory& m) {
    if (m.symbol() == target_symbol_) {
        target_found_ = true;
        // stock_locate will be learned from the first matching AddOrder
    }
}

void ReplayHandler::on_add_order(const AddOrder& m) {
    if (!target_found_) return;
    // Match by stock field rather than stock_locate for reliability
    std::string sym(m.stock.data(), 8);
    auto end = sym.find(' ');
    if (end != std::string::npos) sym.resize(end);
    if (sym != target_symbol_) return;

    // Record the locate for future reference
    if (!is_target(m.stock_locate)) {
        target_locate_ = m.stock_locate;
    }

    Side side = (m.buy_sell == 'B') ? Side::Buy : Side::Sell;
    Order order;
    order.order_id = m.order_ref;
    order.side = side;
    order.kind = OrderKind::Limit;
    order.tif = TimeInForce::GTC;
    order.price = m.price;
    order.quantity = m.shares;
    book_.add_resting(order);
    order_index_[m.order_ref] = {side, m.price};
    ++events_applied_;
    maybe_snapshot(m.timestamp_ns);
}

void ReplayHandler::on_add_order_mpid(const AddOrderMPID& m) {
    // Same as add_order but with MPID field
    if (!target_found_) return;
    std::string sym(m.stock.data(), 8);
    auto end = sym.find(' ');
    if (end != std::string::npos) sym.resize(end);
    if (sym != target_symbol_) return;

    if (!is_target(m.stock_locate)) {
        target_locate_ = m.stock_locate;
    }

    Side side = (m.buy_sell == 'B') ? Side::Buy : Side::Sell;
    Order order;
    order.order_id = m.order_ref;
    order.side = side;
    order.kind = OrderKind::Limit;
    order.tif = TimeInForce::GTC;
    order.price = m.price;
    order.quantity = m.shares;
    book_.add_resting(order);
    order_index_[m.order_ref] = {side, m.price};
    ++events_applied_;
    maybe_snapshot(m.timestamp_ns);
}

void ReplayHandler::on_order_executed(const OrderExecuted& m) {
    if (!is_target(m.stock_locate)) return;
    book_.reduce(m.order_ref, m.shares);
    ++events_applied_;
    maybe_snapshot(m.timestamp_ns);
}

void ReplayHandler::on_order_executed_wp(const OrderExecutedWithPrice& m) {
    if (!is_target(m.stock_locate)) return;
    book_.reduce(m.order_ref, m.shares);
    ++events_applied_;
    maybe_snapshot(m.timestamp_ns);
}

void ReplayHandler::on_order_cancel(const OrderCancel& m) {
    if (!is_target(m.stock_locate)) return;
    book_.reduce(m.order_ref, m.cancelled_shares);
    ++events_applied_;
    maybe_snapshot(m.timestamp_ns);
}

void ReplayHandler::on_order_delete(const OrderDelete& m) {
    if (!is_target(m.stock_locate)) return;
    book_.cancel(m.order_ref);
    order_index_.erase(m.order_ref);
    ++events_applied_;
    maybe_snapshot(m.timestamp_ns);
}

void ReplayHandler::on_order_replace(const OrderReplace& m) {
    if (!is_target(m.stock_locate)) return;

    auto it = order_index_.find(m.original_order_ref);
    if (it == order_index_.end()) return;

    Side side = it->second.side;
    book_.cancel(m.original_order_ref);
    order_index_.erase(it);

    Order order;
    order.order_id = m.new_order_ref;
    order.side = side;
    order.kind = OrderKind::Limit;
    order.tif = TimeInForce::GTC;
    order.price = m.price;
    order.quantity = m.shares;
    book_.add_resting(order);
    order_index_[m.new_order_ref] = {side, m.price};
    ++events_applied_;
    maybe_snapshot(m.timestamp_ns);
}

} // namespace ob::itch
