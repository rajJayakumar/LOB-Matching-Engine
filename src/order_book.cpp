#include <ob/order_book.hpp>

#include <algorithm>
#include <cassert>
#include <limits>

namespace ob {

// ---------------------------------------------------------------------------
// PriceLevel intrusive list operations
// ---------------------------------------------------------------------------

Qty PriceLevel::total_quantity() const {
    Qty total = 0;
    for (Order* o = head; o != nullptr; o = o->next)
        total += o->quantity;
    return total;
}

void PriceLevel::push_back(Order* o) {
    o->next = nullptr;
    o->prev = tail;
    if (tail) tail->next = o;
    else head = o;
    tail = o;
    ++count_;
}

void PriceLevel::erase(Order* o) {
    if (o->prev) o->prev->next = o->next;
    else head = o->next;
    if (o->next) o->next->prev = o->prev;
    else tail = o->prev;
    --count_;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

OrderBook::OrderBook(Price tick_size, std::size_t band_size)
    : tick_size_(tick_size)
    , band_size_(band_size)
    , node_pool_(std::make_unique<FreeListPool>(8192))
    , index_pool_(std::make_unique<FreeListPool>(8192))
    , index_(1 << 18, std::hash<OrderId>{}, std::equal_to<OrderId>{},
             IndexAlloc(index_pool_.get()))
{
    bid_levels_.resize(band_size);
    ask_levels_.resize(band_size);
}

// ---------------------------------------------------------------------------
// Order pool helpers
// ---------------------------------------------------------------------------

Order* OrderBook::alloc_order(const Order& src) {
    auto* o = static_cast<Order*>(node_pool_->allocate(sizeof(Order), alignof(Order)));
    *o = src;
    o->prev = nullptr;
    o->next = nullptr;
    return o;
}

void OrderBook::free_order(Order* o) {
    node_pool_->deallocate(o, sizeof(Order));
}

// ---------------------------------------------------------------------------
// Band initialization
// ---------------------------------------------------------------------------

void OrderBook::init_bid_band(Price first_price) {
    bid_base_ = first_price - static_cast<Price>(band_size_ / 2) * tick_size_;
    bid_init_ = true;
}

void OrderBook::init_ask_band(Price first_price) {
    ask_base_ = first_price - static_cast<Price>(band_size_ / 2) * tick_size_;
    ask_init_ = true;
}

bool OrderBook::bid_in_band(Price p) const {
    if (!bid_init_) return false;
    int idx = bid_idx(p);
    return idx >= 0 && idx < static_cast<int>(band_size_);
}

bool OrderBook::ask_in_band(Price p) const {
    if (!ask_init_) return false;
    int idx = ask_idx(p);
    return idx >= 0 && idx < static_cast<int>(band_size_);
}

int OrderBook::ensure_bid(Price p) {
    if (!bid_init_) [[unlikely]] {
        init_bid_band(p);
        return bid_idx(p);
    }
    if (bid_in_band(p)) [[likely]] return bid_idx(p);
    return -1;  // overflow
}

int OrderBook::ensure_ask(Price p) {
    if (!ask_init_) [[unlikely]] {
        init_ask_band(p);
        return ask_idx(p);
    }
    if (ask_in_band(p)) [[likely]] return ask_idx(p);
    return -1;  // overflow
}

// ---------------------------------------------------------------------------
// Level removal helpers (update best cursor on exhaustion)
// ---------------------------------------------------------------------------

void OrderBook::remove_bid_if_empty(int idx) {
    if (!bid_levels_[idx].empty()) return;
    --bid_count_;

    if (idx != best_bid_idx_) return;
    if (bid_count_ == 0) { best_bid_idx_ = -1; return; }

    for (int i = idx - 1; i >= 0; --i) {
        if (!bid_levels_[i].empty()) {
            best_bid_idx_ = i;
            return;
        }
    }
    best_bid_idx_ = -1;
}

void OrderBook::remove_ask_if_empty(int idx) {
    if (!ask_levels_[idx].empty()) return;
    --ask_count_;

    if (idx != best_ask_idx_) return;
    if (ask_count_ == 0) { best_ask_idx_ = -1; return; }

    for (int i = idx + 1; i < static_cast<int>(band_size_); ++i) {
        if (!ask_levels_[i].empty()) {
            best_ask_idx_ = i;
            return;
        }
    }
    best_ask_idx_ = -1;
}

// ---------------------------------------------------------------------------
// Rest (place a resting order into flat array or overflow)
// ---------------------------------------------------------------------------

void OrderBook::rest(const Order& order) {
    Order* o = alloc_order(order);

    if (order.side == Side::Buy) {
        int idx = ensure_bid(order.price);
        if (idx >= 0) [[likely]] {
            auto& level = bid_levels_[idx];
            bool was_empty = level.empty();
            level.price = order.price;
            level.push_back(o);
            index_[order.order_id] = {order.side, order.price, o};
            if (was_empty) {
                ++bid_count_;
                if (best_bid_idx_ < 0 || idx > best_bid_idx_) {
                    best_bid_idx_ = idx;
                }
            }
        } else {
            auto [it, inserted] = bid_overflow_.try_emplace(order.price);
            auto& level = it->second;
            level.price = order.price;
            level.push_back(o);
            index_[order.order_id] = {order.side, order.price, o};
        }
    } else {
        int idx = ensure_ask(order.price);
        if (idx >= 0) [[likely]] {
            auto& level = ask_levels_[idx];
            bool was_empty = level.empty();
            level.price = order.price;
            level.push_back(o);
            index_[order.order_id] = {order.side, order.price, o};
            if (was_empty) {
                ++ask_count_;
                if (best_ask_idx_ < 0 || idx < best_ask_idx_) {
                    best_ask_idx_ = idx;
                }
            }
        } else {
            auto [it, inserted] = ask_overflow_.try_emplace(order.price);
            auto& level = it->second;
            level.price = order.price;
            level.push_back(o);
            index_[order.order_id] = {order.side, order.price, o};
        }
    }
}

// ---------------------------------------------------------------------------
// Add (match + rest)
// ---------------------------------------------------------------------------

std::vector<Trade> OrderBook::add(Order order) {
    if (order.quantity == 0) [[unlikely]] return {};

    order.sequence = next_sequence_++;
    if (order.original_quantity == 0) {
        order.original_quantity = order.quantity;
    }

    // FOK pre-scan
    if (order.tif == TimeInForce::FOK) [[unlikely]] {
        if (order.side == Side::Buy) {
            if (!can_fill_asks(order)) return {};
        } else {
            if (!can_fill_bids(order)) return {};
        }
    }

    std::vector<Trade> trades;

    if (order.side == Side::Buy) {
        match_against_asks(order, trades);
    } else {
        match_against_bids(order, trades);
    }

    // Rest any remaining quantity for limit GTC orders
    if (order.quantity > 0 && order.kind == OrderKind::Limit && order.tif == TimeInForce::GTC) {
        rest(order);
    }

    return trades;
}

// ---------------------------------------------------------------------------
// Matching
// ---------------------------------------------------------------------------

void OrderBook::match_against_asks(Order& aggressor, std::vector<Trade>& trades) {
    if (best_ask_idx_ < 0) return;

    int idx = best_ask_idx_;
    int end = static_cast<int>(band_size_);

    while (idx < end && aggressor.quantity > 0) {
        auto& level = ask_levels_[idx];
        if (level.empty()) {
            ++idx;
            continue;
        }

        Price level_price = ask_price(idx);
        if (aggressor.kind == OrderKind::Limit && aggressor.price < level_price) break;

        Order* o = level.head;
        while (o != nullptr && aggressor.quantity > 0) {
            Qty fill_qty = std::min(aggressor.quantity, o->quantity);

            trades.push_back({aggressor.order_id, o->order_id,
                              o->price, fill_qty, aggressor.sequence});

            aggressor.quantity -= fill_qty;
            o->quantity -= fill_qty;

            if (o->quantity == 0) {
                Order* next = o->next;
                index_.erase(o->order_id);
                level.erase(o);
                free_order(o);
                o = next;
            } else {
                o = o->next;
            }
        }

        if (level.empty()) {
            --ask_count_;
        }
        ++idx;
    }

    // Update best_ask_idx_
    if (best_ask_idx_ >= 0 && ask_levels_[best_ask_idx_].empty()) {
        if (ask_count_ == 0) {
            best_ask_idx_ = -1;
        } else {
            int saved = best_ask_idx_;
            best_ask_idx_ = -1;
            for (int i = saved + 1; i < end; ++i) {
                if (!ask_levels_[i].empty()) {
                    best_ask_idx_ = i;
                    break;
                }
            }
        }
    }
}

void OrderBook::match_against_bids(Order& aggressor, std::vector<Trade>& trades) {
    if (best_bid_idx_ < 0) return;

    int idx = best_bid_idx_;

    while (idx >= 0 && aggressor.quantity > 0) {
        auto& level = bid_levels_[idx];
        if (level.empty()) {
            --idx;
            continue;
        }

        Price level_price = bid_price(idx);
        if (aggressor.kind == OrderKind::Limit && aggressor.price > level_price) break;

        Order* o = level.head;
        while (o != nullptr && aggressor.quantity > 0) {
            Qty fill_qty = std::min(aggressor.quantity, o->quantity);

            trades.push_back({aggressor.order_id, o->order_id,
                              o->price, fill_qty, aggressor.sequence});

            aggressor.quantity -= fill_qty;
            o->quantity -= fill_qty;

            if (o->quantity == 0) {
                Order* next = o->next;
                index_.erase(o->order_id);
                level.erase(o);
                free_order(o);
                o = next;
            } else {
                o = o->next;
            }
        }

        if (level.empty()) {
            --bid_count_;
        }
        --idx;
    }

    // Update best_bid_idx_
    if (best_bid_idx_ >= 0 && bid_levels_[best_bid_idx_].empty()) {
        if (bid_count_ == 0) {
            best_bid_idx_ = -1;
        } else {
            int saved = best_bid_idx_;
            best_bid_idx_ = -1;
            for (int i = saved - 1; i >= 0; --i) {
                if (!bid_levels_[i].empty()) {
                    best_bid_idx_ = i;
                    break;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Cancel
// ---------------------------------------------------------------------------

bool OrderBook::cancel(OrderId id) {
    auto idx_it = index_.find(id);
    if (idx_it == index_.end()) [[unlikely]] return false;

    auto& loc = idx_it->second;
    Order* o = loc.order;

    if (loc.side == Side::Buy) {
        if (bid_in_band(loc.price)) {
            int idx = bid_idx(loc.price);
            bid_levels_[idx].erase(o);
            remove_bid_if_empty(idx);
        } else {
            auto ov = bid_overflow_.find(loc.price);
            if (ov != bid_overflow_.end()) {
                ov->second.erase(o);
                if (ov->second.empty()) {
                    bid_overflow_.erase(ov);
                }
            }
        }
    } else {
        if (ask_in_band(loc.price)) {
            int idx = ask_idx(loc.price);
            ask_levels_[idx].erase(o);
            remove_ask_if_empty(idx);
        } else {
            auto ov = ask_overflow_.find(loc.price);
            if (ov != ask_overflow_.end()) {
                ov->second.erase(o);
                if (ov->second.empty()) {
                    ask_overflow_.erase(ov);
                }
            }
        }
    }
    free_order(o);
    index_.erase(idx_it);
    return true;
}

// ---------------------------------------------------------------------------
// Reduce
// ---------------------------------------------------------------------------

bool OrderBook::reduce(OrderId id, Qty qty) {
    auto idx_it = index_.find(id);
    if (idx_it == index_.end()) return false;

    Order* o = idx_it->second.order;
    if (qty >= o->quantity) {
        cancel(id);
    } else {
        o->quantity -= qty;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Modify
// ---------------------------------------------------------------------------

std::vector<Trade> OrderBook::modify(OrderId id, Price new_price, Qty new_qty) {
    auto idx_it = index_.find(id);
    if (idx_it == index_.end()) return {};

    auto& loc = idx_it->second;
    Side side = loc.side;
    OrderId oid = id;

    // If price unchanged, just update quantity in place (keeps time priority)
    if (new_price == loc.price) {
        loc.order->quantity = new_qty;
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

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

void OrderBook::clear() {
    auto dealloc_level = [this](PriceLevel& level) {
        Order* o = level.head;
        while (o) {
            Order* next = o->next;
            free_order(o);
            o = next;
        }
        level.head = level.tail = nullptr;
        level.count_ = 0;
    };
    for (auto& level : bid_levels_) dealloc_level(level);
    for (auto& level : ask_levels_) dealloc_level(level);
    for (auto& [_, level] : bid_overflow_) dealloc_level(level);
    for (auto& [_, level] : ask_overflow_) dealloc_level(level);
    bid_overflow_.clear();
    ask_overflow_.clear();
    index_.clear();
    best_bid_idx_ = -1;
    best_ask_idx_ = -1;
    bid_count_ = 0;
    ask_count_ = 0;
    bid_init_ = false;
    ask_init_ = false;
}

// ---------------------------------------------------------------------------
// Add resting (no matching)
// ---------------------------------------------------------------------------

void OrderBook::add_resting(Order order) {
    if (order.quantity == 0) return;
    order.sequence = next_sequence_++;
    if (order.original_quantity == 0) {
        order.original_quantity = order.quantity;
    }
    rest(order);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::optional<Price> OrderBook::best_bid() const {
    std::optional<Price> flat_best;
    if (best_bid_idx_ >= 0) flat_best = bid_price(best_bid_idx_);

    if (!bid_overflow_.empty()) {
        Price ov_best = bid_overflow_.begin()->first;
        if (!flat_best || ov_best > *flat_best) return ov_best;
    }
    return flat_best;
}

std::optional<Price> OrderBook::best_ask() const {
    std::optional<Price> flat_best;
    if (best_ask_idx_ >= 0) flat_best = ask_price(best_ask_idx_);

    if (!ask_overflow_.empty()) {
        Price ov_best = ask_overflow_.begin()->first;
        if (!flat_best || ov_best < *flat_best) return ov_best;
    }
    return flat_best;
}

std::vector<LevelSnapshot> OrderBook::top_n_bids(std::size_t n) const {
    std::vector<LevelSnapshot> result;
    result.reserve(n);

    int fi = best_bid_idx_;
    auto oi = bid_overflow_.begin();
    auto oe = bid_overflow_.end();

    while (result.size() < n && (fi >= 0 || oi != oe)) {
        Price fp = (fi >= 0) ? bid_price(fi) : std::numeric_limits<Price>::min();
        Price op = (oi != oe) ? oi->first : std::numeric_limits<Price>::min();

        if (fi >= 0 && fp >= op) {
            if (!bid_levels_[fi].empty()) {
                result.push_back({fp, bid_levels_[fi].total_quantity(),
                                  bid_levels_[fi].order_count()});
            }
            --fi;
            while (fi >= 0 && bid_levels_[fi].empty()) --fi;
        } else {
            result.push_back({oi->first, oi->second.total_quantity(),
                              oi->second.order_count()});
            ++oi;
        }
    }
    return result;
}

std::vector<LevelSnapshot> OrderBook::top_n_asks(std::size_t n) const {
    std::vector<LevelSnapshot> result;
    result.reserve(n);

    int fi = best_ask_idx_;
    int fend = static_cast<int>(band_size_);
    auto oi = ask_overflow_.begin();
    auto oe = ask_overflow_.end();

    while (result.size() < n && (fi >= 0 || oi != oe)) {
        Price fp = (fi >= 0) ? ask_price(fi) : std::numeric_limits<Price>::max();
        Price op = (oi != oe) ? oi->first : std::numeric_limits<Price>::max();

        if (fi >= 0 && fp <= op) {
            if (!ask_levels_[fi].empty()) {
                result.push_back({fp, ask_levels_[fi].total_quantity(),
                                  ask_levels_[fi].order_count()});
            }
            ++fi;
            while (fi < fend && ask_levels_[fi].empty()) ++fi;
            if (fi >= fend) fi = -1;
        } else {
            result.push_back({oi->first, oi->second.total_quantity(),
                              oi->second.order_count()});
            ++oi;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// FOK pre-scan
// ---------------------------------------------------------------------------

bool OrderBook::can_fill_asks(const Order& order) const {
    if (best_ask_idx_ < 0) return false;
    Qty remaining = order.quantity;
    for (int i = best_ask_idx_; i < static_cast<int>(band_size_) && remaining > 0; ++i) {
        if (ask_levels_[i].empty()) continue;
        Price level_price = ask_price(i);
        if (order.kind == OrderKind::Limit && order.price < level_price) break;
        for (Order* o = ask_levels_[i].head; o != nullptr; o = o->next) {
            Qty fill = std::min(remaining, o->quantity);
            remaining -= fill;
            if (remaining == 0) return true;
        }
    }
    return remaining == 0;
}

bool OrderBook::can_fill_bids(const Order& order) const {
    if (best_bid_idx_ < 0) return false;
    Qty remaining = order.quantity;
    for (int i = best_bid_idx_; i >= 0 && remaining > 0; --i) {
        if (bid_levels_[i].empty()) continue;
        Price level_price = bid_price(i);
        if (order.kind == OrderKind::Limit && order.price > level_price) break;
        for (Order* o = bid_levels_[i].head; o != nullptr; o = o->next) {
            Qty fill = std::min(remaining, o->quantity);
            remaining -= fill;
            if (remaining == 0) return true;
        }
    }
    return remaining == 0;
}

} // namespace ob
