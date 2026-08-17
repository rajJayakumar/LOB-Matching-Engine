#pragma once

#include <ob/order.hpp>
#include <ob/trade.hpp>

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ob {

struct PriceLevel {
    Price price = 0;
    std::list<Order> orders;

    Qty total_quantity() const;
    std::size_t order_count() const { return orders.size(); }
};

struct LevelSnapshot {
    Price price = 0;
    Qty   total_qty = 0;
    std::size_t order_count = 0;
};

struct Locator {
    Side  side;
    Price price;
    std::list<Order>::iterator it;
};

class OrderBook {
public:
    std::vector<Trade> add(Order order);
    bool cancel(OrderId id);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;

    std::vector<LevelSnapshot> top_n_bids(std::size_t n) const;
    std::vector<LevelSnapshot> top_n_asks(std::size_t n) const;

    std::size_t bid_level_count() const { return bids_.size(); }
    std::size_t ask_level_count() const { return asks_.size(); }
    std::size_t order_count() const { return index_.size(); }

private:
    // Bids: highest price first (best = begin)
    std::map<Price, PriceLevel, std::greater<>> bids_;
    // Asks: lowest price first (best = begin)
    std::map<Price, PriceLevel> asks_;
    // O(1) cancel lookup
    std::unordered_map<OrderId, Locator> index_;

    std::uint64_t next_sequence_ = 1;
};

} // namespace ob
