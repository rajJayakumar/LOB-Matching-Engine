#include <ob/order_book.hpp>

namespace ob {

Qty PriceLevel::total_quantity() const {
    Qty total = 0;
    for (const auto& o : orders) {
        total += o.quantity;
    }
    return total;
}

std::vector<Trade> OrderBook::add(Order order) {
    if (order.quantity == 0) return {};

    order.sequence = next_sequence_++;
    if (order.original_quantity == 0) {
        order.original_quantity = order.quantity;
    }

    // FOK pre-scan: reject if the entire quantity is not immediately fillable
    if (order.tif == TimeInForce::FOK) {
        if (order.side == Side::Buy) {
            if (!can_fill(order, asks_)) return {};
        } else {
            if (!can_fill(order, bids_)) return {};
        }
    }

    std::vector<Trade> trades;

    if (order.side == Side::Buy) {
        match_against(order, asks_, trades);
    } else {
        match_against(order, bids_, trades);
    }

    // Rest any remaining quantity for limit GTC orders
    if (order.quantity > 0 && order.kind == OrderKind::Limit && order.tif == TimeInForce::GTC) {
        rest(order);
    }

    return trades;
}

template <typename BookSide>
void OrderBook::match_against(Order& aggressor, BookSide& opposite, std::vector<Trade>& trades) {
    auto it = opposite.begin();
    while (it != opposite.end() && aggressor.quantity > 0) {
        // Check price crossing
        if (aggressor.kind == OrderKind::Limit) {
            if (aggressor.side == Side::Buy && aggressor.price < it->first) break;
            if (aggressor.side == Side::Sell && aggressor.price > it->first) break;
        }

        auto& level = it->second;
        auto order_it = level.orders.begin();
        while (order_it != level.orders.end() && aggressor.quantity > 0) {
            Qty fill_qty = std::min(aggressor.quantity, order_it->quantity);

            trades.push_back({aggressor.order_id, order_it->order_id,
                              order_it->price, fill_qty, aggressor.sequence});

            aggressor.quantity -= fill_qty;
            order_it->quantity -= fill_qty;

            if (order_it->quantity == 0) {
                index_.erase(order_it->order_id);
                order_it = level.orders.erase(order_it);
            } else {
                ++order_it;
            }
        }

        if (level.orders.empty()) {
            it = opposite.erase(it);
        } else {
            ++it;
        }
    }
}

void OrderBook::rest(const Order& order) {
    if (order.side == Side::Buy) {
        auto& level = bids_[order.price];
        level.price = order.price;
        level.orders.push_back(order);
        index_[order.order_id] = {order.side, order.price, std::prev(level.orders.end())};
    } else {
        auto& level = asks_[order.price];
        level.price = order.price;
        level.orders.push_back(order);
        index_[order.order_id] = {order.side, order.price, std::prev(level.orders.end())};
    }
}

template <typename BookSide>
bool OrderBook::can_fill(const Order& order, const BookSide& opposite) const {
    Qty remaining = order.quantity;
    for (auto it = opposite.begin(); it != opposite.end() && remaining > 0; ++it) {
        if (order.kind == OrderKind::Limit) {
            if (order.side == Side::Buy && order.price < it->first) break;
            if (order.side == Side::Sell && order.price > it->first) break;
        }
        for (const auto& resting : it->second.orders) {
            Qty fill = std::min(remaining, resting.quantity);
            remaining -= fill;
            if (remaining == 0) return true;
        }
    }
    return remaining == 0;
}

bool OrderBook::cancel(OrderId id) {
    auto idx_it = index_.find(id);
    if (idx_it == index_.end()) return false;

    auto& loc = idx_it->second;
    if (loc.side == Side::Buy) {
        auto map_it = bids_.find(loc.price);
        map_it->second.orders.erase(loc.it);
        if (map_it->second.orders.empty()) {
            bids_.erase(map_it);
        }
    } else {
        auto map_it = asks_.find(loc.price);
        map_it->second.orders.erase(loc.it);
        if (map_it->second.orders.empty()) {
            asks_.erase(map_it);
        }
    }
    index_.erase(idx_it);
    return true;
}

bool OrderBook::reduce(OrderId id, Qty qty) {
    auto idx_it = index_.find(id);
    if (idx_it == index_.end()) return false;

    auto& order = *idx_it->second.it;
    if (qty >= order.quantity) {
        cancel(id);
    } else {
        order.quantity -= qty;
    }
    return true;
}

std::vector<Trade> OrderBook::modify(OrderId id, Price new_price, Qty new_qty) {
    auto idx_it = index_.find(id);
    if (idx_it == index_.end()) return {};

    auto& loc = idx_it->second;
    Side side = loc.side;
    OrderId oid = id;

    // If price unchanged, just update quantity in place (keeps time priority)
    if (new_price == loc.price) {
        loc.it->quantity = new_qty;
        if (new_qty == 0) {
            cancel(id);
        }
        return {};
    }

    // Price change: cancel + re-add (loses time priority)
    cancel(id);
    Order new_order;
    new_order.order_id = oid;
    new_order.side     = side;
    new_order.kind     = OrderKind::Limit;
    new_order.tif      = TimeInForce::GTC;
    new_order.price    = new_price;
    new_order.quantity = new_qty;
    return add(new_order);
}

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
    index_.clear();
}

void OrderBook::add_resting(Order order) {
    if (order.quantity == 0) return;
    order.sequence = next_sequence_++;
    if (order.original_quantity == 0) {
        order.original_quantity = order.quantity;
    }
    rest(order);
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::vector<LevelSnapshot> OrderBook::top_n_bids(std::size_t n) const {
    std::vector<LevelSnapshot> result;
    result.reserve(n);
    for (auto it = bids_.begin(); it != bids_.end() && result.size() < n; ++it) {
        result.push_back({it->first, it->second.total_quantity(), it->second.order_count()});
    }
    return result;
}

std::vector<LevelSnapshot> OrderBook::top_n_asks(std::size_t n) const {
    std::vector<LevelSnapshot> result;
    result.reserve(n);
    for (auto it = asks_.begin(); it != asks_.end() && result.size() < n; ++it) {
        result.push_back({it->first, it->second.total_quantity(), it->second.order_count()});
    }
    return result;
}

} // namespace ob
