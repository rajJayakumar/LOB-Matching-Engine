#pragma once

#include <ob/order.hpp>
#include <ob/pool.hpp>
#include <ob/trade.hpp>

#include <cassert>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ob {

struct PriceLevel {
    Price price = 0;
    Order* head = nullptr;
    Order* tail = nullptr;
    std::size_t count_ = 0;

    bool empty() const { return head == nullptr; }
    std::size_t order_count() const { return count_; }
    Qty total_quantity() const;

    void push_back(Order* o);
    void erase(Order* o);
};

struct LevelSnapshot {
    Price price = 0;
    Qty   total_qty = 0;
    std::size_t order_count = 0;
};

struct Locator {
    Side   side;
    Price  price;
    Order* order;
};

class OrderBook {
public:
    explicit OrderBook(Price tick_size = 1, std::size_t band_size = 4096);

    std::vector<Trade> add(Order order);
    bool cancel(OrderId id);
    bool reduce(OrderId id, Qty qty);
    std::vector<Trade> modify(OrderId id, Price new_price, Qty new_qty);
    void clear();

    // Add a resting order directly (no matching). Used by feed replay.
    void add_resting(Order order);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;

    std::vector<LevelSnapshot> top_n_bids(std::size_t n) const;
    std::vector<LevelSnapshot> top_n_asks(std::size_t n) const;

    std::size_t bid_level_count() const { return bid_count_ + bid_overflow_.size(); }
    std::size_t ask_level_count() const { return ask_count_ + ask_overflow_.size(); }
    std::size_t order_count() const { return index_.size(); }

private:
    Price tick_size_;
    std::size_t band_size_;

    // Pools for fixed-size allocations — declared before the containers they
    // back so they are destroyed after them (reverse declaration order).
    std::unique_ptr<FreeListPool> node_pool_;   // Order objects
    std::unique_ptr<FreeListPool> index_pool_;  // unordered_map nodes

    // Flat tick-indexed arrays for each side.
    std::vector<PriceLevel> bid_levels_;
    std::vector<PriceLevel> ask_levels_;

    Price bid_base_ = 0;
    Price ask_base_ = 0;

    int best_bid_idx_ = -1;
    int best_ask_idx_ = -1;

    std::size_t bid_count_ = 0;
    std::size_t ask_count_ = 0;

    bool bid_init_ = false;
    bool ask_init_ = false;

    // Overflow maps for prices too far from the current band.
    std::map<Price, PriceLevel, std::greater<>> bid_overflow_;
    std::map<Price, PriceLevel> ask_overflow_;

    // O(1) cancel lookup — pool-allocated to avoid per-node malloc
    using IndexAlloc = PoolAllocator<std::pair<const OrderId, Locator>>;
    std::unordered_map<OrderId, Locator, std::hash<OrderId>,
                       std::equal_to<OrderId>, IndexAlloc> index_;

    std::uint64_t next_sequence_ = 1;

    // Index helpers
    int bid_idx(Price p) const { return static_cast<int>((p - bid_base_) / tick_size_); }
    int ask_idx(Price p) const { return static_cast<int>((p - ask_base_) / tick_size_); }
    Price bid_price(int idx) const { return bid_base_ + static_cast<Price>(idx) * tick_size_; }
    Price ask_price(int idx) const { return ask_base_ + static_cast<Price>(idx) * tick_size_; }

    bool bid_in_band(Price p) const;
    bool ask_in_band(Price p) const;

    void init_bid_band(Price first_price);
    void init_ask_band(Price first_price);

    int ensure_bid(Price p);
    int ensure_ask(Price p);

    void remove_bid_if_empty(int idx);
    void remove_ask_if_empty(int idx);

    Order* alloc_order(const Order& src);
    void free_order(Order* o);

    void rest(const Order& order);

    void match_against_asks(Order& aggressor, std::vector<Trade>& trades);
    void match_against_bids(Order& aggressor, std::vector<Trade>& trades);

    bool can_fill_asks(const Order& order) const;
    bool can_fill_bids(const Order& order) const;
};

} // namespace ob
