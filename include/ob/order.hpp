#pragma once

// All prices are signed 64-bit integers. Never floating point.
// Fixed-point scale depends on source (Databento 1e-9, ITCH 1e-4) — never mix.

#include <cstdint>

namespace ob {

using Price   = std::int64_t;
using Qty     = std::uint64_t;
using OrderId = std::uint64_t;

enum class Side : std::uint8_t { Buy, Sell };

enum class TimeInForce : std::uint8_t { GTC, IOC, FOK };

enum class OrderKind : std::uint8_t { Limit, Market };

struct Order {
    // Hot fields — accessed on every match/cancel (first cache line)
    Qty         quantity = 0;       // remaining quantity
    Order*      next = nullptr;     // intrusive list: iteration + unlink
    Order*      prev = nullptr;     // intrusive list: unlink
    OrderId     order_id = 0;
    Price       price    = 0;       // ignored / 0 for market orders
    Side        side     = Side::Buy;
    OrderKind   kind     = OrderKind::Limit;
    TimeInForce tif      = TimeInForce::GTC;
    // 5 bytes padding

    // Cold fields — set once, not on hot path
    Qty         original_quantity = 0;
    std::uint64_t sequence = 0;     // monotonic, for time priority
};

static_assert(sizeof(Order) == 64, "Order must fit in one cache line");

} // namespace ob
