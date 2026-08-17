#include <ob/order_book.hpp>

namespace ob {

Qty PriceLevel::total_quantity() const {
    Qty total = 0;
    for (const auto& o : orders) {
        total += o.quantity;
    }
    return total;
}

std::vector<Trade> OrderBook::add(Order /*order*/) {
    // Stub — matching logic added in Task 1.3.
    return {};
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
