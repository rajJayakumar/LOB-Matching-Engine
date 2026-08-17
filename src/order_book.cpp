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
    order.sequence = next_sequence_++;
    if (order.original_quantity == 0) {
        order.original_quantity = order.quantity;
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

bool OrderBook::cancel(OrderId /*id*/) {
    // Stub — cancel logic added in Task 1.9.
    return false;
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
