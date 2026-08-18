#pragma once

// All prices are signed 64-bit integers. Never floating point.
// Fixed-point scale depends on source (Databento 1e-9, ITCH 1e-4) — never mix.

#include <cstdint>

namespace ob {

using Price   = std::int64_t;
using Qty     = std::uint64_t;
using OrderId = std::uint64_t;

enum class Side { Buy, Sell };

enum class TimeInForce { GTC, IOC, FOK };

enum class OrderKind { Limit, Market };

struct Order {
    OrderId     order_id = 0;
    Side        side     = Side::Buy;
    OrderKind   kind     = OrderKind::Limit;
    TimeInForce tif      = TimeInForce::GTC;
    Price       price    = 0;       // ignored / 0 for market orders
    Qty         quantity = 0;       // remaining quantity
    Qty         original_quantity = 0;
    std::uint64_t sequence = 0;     // monotonic, for time priority

    // Intrusive doubly-linked list pointers (per-level FIFO)
    Order*      prev = nullptr;
    Order*      next = nullptr;
};

} // namespace ob
