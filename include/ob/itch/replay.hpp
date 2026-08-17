#pragma once

// ITCH 5.0 replay: maps parser callbacks to the OrderBook engine for a
// target symbol. Filters via the Stock Directory (R) message.

#include <ob/itch/parser.hpp>
#include <ob/order_book.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace ob::itch {

// Callback invoked after each book-mutating event
using SnapshotCallback = std::function<void(std::uint64_t timestamp_ns, const OrderBook& book)>;

class ReplayHandler : public Handler {
public:
    explicit ReplayHandler(const std::string& target_symbol,
                          Price tick_size = 1, std::size_t band_size = 4096);

    // Set a callback for periodic snapshots (e.g., print top-of-book)
    void set_snapshot_callback(SnapshotCallback cb, std::size_t interval = 100000);

    OrderBook& book() { return book_; }
    const OrderBook& book() const { return book_; }
    std::size_t events_applied() const { return events_applied_; }

    void on_stock_directory(const StockDirectory& m) override;
    void on_add_order(const AddOrder& m) override;
    void on_add_order_mpid(const AddOrderMPID& m) override;
    void on_order_executed(const OrderExecuted& m) override;
    void on_order_executed_wp(const OrderExecutedWithPrice& m) override;
    void on_order_cancel(const OrderCancel& m) override;
    void on_order_delete(const OrderDelete& m) override;
    void on_order_replace(const OrderReplace& m) override;

private:
    bool is_target(std::uint16_t stock_locate) const;
    void maybe_snapshot(std::uint64_t ts);

    std::string target_symbol_;
    std::uint16_t target_locate_ = 0;
    bool target_found_ = false;

    OrderBook book_;
    std::size_t events_applied_ = 0;

    // Order-ID → (side, price) index for E/C/X/D/U which lack price/side
    struct OrderInfo {
        Side side;
        Price price;
    };
    std::unordered_map<std::uint64_t, OrderInfo> order_index_;

    SnapshotCallback snapshot_cb_;
    std::size_t snapshot_interval_ = 0;
};

} // namespace ob::itch
