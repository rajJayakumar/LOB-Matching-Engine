#pragma once

#include <cstdint>
#include <ob/order.hpp>

namespace ob {

struct Trade {
    OrderId  aggressor_id = 0;
    OrderId  resting_id   = 0;
    Price    price        = 0;
    Qty      quantity     = 0;
    std::uint64_t sequence = 0;
};

} // namespace ob
