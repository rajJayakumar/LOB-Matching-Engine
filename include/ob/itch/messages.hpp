#pragma once

// NASDAQ TotalView-ITCH 5.0 message types and big-endian decoding.
// Field offsets follow the official spec (binary-file framing, 2-byte BE length prefix).
// Prices are 4-byte unsigned integers in units of 1e-4 — stored as raw int64 (never float).
// Timestamps are 6-byte nanoseconds since midnight.

#include <cstdint>
#include <cstring>
#include <array>

namespace ob::itch {

// --- Big-endian decode helpers ---

inline std::uint16_t read_be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) << 8 |
           static_cast<std::uint16_t>(p[1]);
}

inline std::uint32_t read_be32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) << 24 |
           static_cast<std::uint32_t>(p[1]) << 16 |
           static_cast<std::uint32_t>(p[2]) << 8  |
           static_cast<std::uint32_t>(p[3]);
}

inline std::uint64_t read_be48(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(p[0]) << 40 |
           static_cast<std::uint64_t>(p[1]) << 32 |
           static_cast<std::uint64_t>(p[2]) << 24 |
           static_cast<std::uint64_t>(p[3]) << 16 |
           static_cast<std::uint64_t>(p[4]) << 8  |
           static_cast<std::uint64_t>(p[5]);
}

inline std::uint64_t read_be64(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(p[0]) << 56 |
           static_cast<std::uint64_t>(p[1]) << 48 |
           static_cast<std::uint64_t>(p[2]) << 40 |
           static_cast<std::uint64_t>(p[3]) << 32 |
           static_cast<std::uint64_t>(p[4]) << 24 |
           static_cast<std::uint64_t>(p[5]) << 16 |
           static_cast<std::uint64_t>(p[6]) << 8  |
           static_cast<std::uint64_t>(p[7]);
}

// --- Message type codes ---
enum class MsgType : char {
    SystemEvent     = 'S',
    StockDirectory  = 'R',
    AddOrder        = 'A',
    AddOrderMPID    = 'F',
    OrderExecuted   = 'E',
    OrderExecutedWP = 'C',  // Executed with price
    OrderCancel     = 'X',
    OrderDelete     = 'D',
    OrderReplace    = 'U',
    TradeNonCross   = 'P',
    CrossTrade      = 'Q',
    BrokenTrade     = 'B',
    NOII            = 'I',
    StockTradingAction = 'H',
    RegSHO          = 'Y',
    MarketParticipantPosition = 'L',
    MWCBDecline     = 'V',
    MWCBStatus      = 'W',
    IPOQuoting      = 'K',
    LULDAuctionCollar = 'J',
    OperationalHalt = 'h',
};

// --- Decoded message structs ---
// All offsets are from byte 0 of the message body (after the 2-byte length prefix).
// Byte 0 is always the message type character.

struct SystemEvent {
    std::uint64_t timestamp_ns;  // 6-byte ns since midnight
    char event_code;             // 'O','S','Q','M','E','C'

    static SystemEvent decode(const std::uint8_t* buf) {
        // Offset: 0=type(1), 1=stock_locate(2), 3=tracking(2), 5=timestamp(6), 11=event_code(1)
        SystemEvent m;
        m.timestamp_ns = read_be48(buf + 5);
        m.event_code = static_cast<char>(buf[11]);
        return m;
    }
};

struct StockDirectory {
    std::uint64_t timestamp_ns;
    std::array<char, 8> stock;   // Right-padded with spaces
    char market_category;
    char financial_status;

    static StockDirectory decode(const std::uint8_t* buf) {
        // Offset: 0=type(1), 1=stock_locate(2), 3=tracking(2), 5=timestamp(6),
        // 11=stock(8), 19=market_category(1), 20=financial_status(1), ...
        StockDirectory m;
        m.timestamp_ns = read_be48(buf + 5);
        std::memcpy(m.stock.data(), buf + 11, 8);
        m.market_category = static_cast<char>(buf[19]);
        m.financial_status = static_cast<char>(buf[20]);
        return m;
    }

    // Return stock symbol trimmed of trailing spaces
    std::string symbol() const {
        auto end = stock.data() + 8;
        while (end > stock.data() && *(end - 1) == ' ') --end;
        return std::string(stock.data(), end);
    }
};

struct AddOrder {
    std::uint16_t stock_locate;
    std::uint64_t timestamp_ns;
    std::uint64_t order_ref;     // Order reference number
    char          buy_sell;      // 'B' or 'S'
    std::uint32_t shares;
    std::array<char, 8> stock;
    std::int64_t  price;         // 4-byte price in 1e-4 units, stored as int64

    static AddOrder decode(const std::uint8_t* buf) {
        // Offset: 0=type(1), 1=stock_locate(2), 3=tracking(2), 5=timestamp(6),
        // 11=order_ref(8), 19=buy_sell(1), 20=shares(4), 24=stock(8), 32=price(4)
        AddOrder m;
        m.stock_locate = read_be16(buf + 1);
        m.timestamp_ns = read_be48(buf + 5);
        m.order_ref = read_be64(buf + 11);
        m.buy_sell = static_cast<char>(buf[19]);
        m.shares = read_be32(buf + 20);
        std::memcpy(m.stock.data(), buf + 24, 8);
        m.price = static_cast<std::int64_t>(read_be32(buf + 32));
        return m;
    }
};

struct AddOrderMPID {
    std::uint16_t stock_locate;
    std::uint64_t timestamp_ns;
    std::uint64_t order_ref;
    char          buy_sell;
    std::uint32_t shares;
    std::array<char, 8> stock;
    std::int64_t  price;
    std::array<char, 4> mpid;

    static AddOrderMPID decode(const std::uint8_t* buf) {
        // Offset: same as AddOrder through price(4), then 36=mpid(4)
        // Total: 40 bytes
        AddOrderMPID m;
        m.stock_locate = read_be16(buf + 1);
        m.timestamp_ns = read_be48(buf + 5);
        m.order_ref = read_be64(buf + 11);
        m.buy_sell = static_cast<char>(buf[19]);
        m.shares = read_be32(buf + 20);
        std::memcpy(m.stock.data(), buf + 24, 8);
        m.price = static_cast<std::int64_t>(read_be32(buf + 32));
        std::memcpy(m.mpid.data(), buf + 36, 4);
        return m;
    }
};

struct OrderExecuted {
    std::uint16_t stock_locate;
    std::uint64_t timestamp_ns;
    std::uint64_t order_ref;
    std::uint32_t shares;
    std::uint64_t match_number;

    static OrderExecuted decode(const std::uint8_t* buf) {
        // Offset: 0=type(1), 1=stock_locate(2), 3=tracking(2), 5=timestamp(6),
        // 11=order_ref(8), 19=shares(4), 23=match_number(8)
        OrderExecuted m;
        m.stock_locate = read_be16(buf + 1);
        m.timestamp_ns = read_be48(buf + 5);
        m.order_ref = read_be64(buf + 11);
        m.shares = read_be32(buf + 19);
        m.match_number = read_be64(buf + 23);
        return m;
    }
};

struct OrderExecutedWithPrice {
    std::uint16_t stock_locate;
    std::uint64_t timestamp_ns;
    std::uint64_t order_ref;
    std::uint32_t shares;
    std::uint64_t match_number;
    char          printable;
    std::int64_t  price;

    static OrderExecutedWithPrice decode(const std::uint8_t* buf) {
        // Offset: 0=type(1), 1=stock_locate(2), 3=tracking(2), 5=timestamp(6),
        // 11=order_ref(8), 19=shares(4), 23=match_number(8), 31=printable(1), 32=price(4)
        OrderExecutedWithPrice m;
        m.stock_locate = read_be16(buf + 1);
        m.timestamp_ns = read_be48(buf + 5);
        m.order_ref = read_be64(buf + 11);
        m.shares = read_be32(buf + 19);
        m.match_number = read_be64(buf + 23);
        m.printable = static_cast<char>(buf[31]);
        m.price = static_cast<std::int64_t>(read_be32(buf + 32));
        return m;
    }
};

struct OrderCancel {
    std::uint16_t stock_locate;
    std::uint64_t timestamp_ns;
    std::uint64_t order_ref;
    std::uint32_t cancelled_shares;

    static OrderCancel decode(const std::uint8_t* buf) {
        // Offset: 0=type(1), 1=stock_locate(2), 3=tracking(2), 5=timestamp(6),
        // 11=order_ref(8), 19=cancelled_shares(4)
        OrderCancel m;
        m.stock_locate = read_be16(buf + 1);
        m.timestamp_ns = read_be48(buf + 5);
        m.order_ref = read_be64(buf + 11);
        m.cancelled_shares = read_be32(buf + 19);
        return m;
    }
};

struct OrderDelete {
    std::uint16_t stock_locate;
    std::uint64_t timestamp_ns;
    std::uint64_t order_ref;

    static OrderDelete decode(const std::uint8_t* buf) {
        // Offset: 0=type(1), 1=stock_locate(2), 3=tracking(2), 5=timestamp(6),
        // 11=order_ref(8)
        OrderDelete m;
        m.stock_locate = read_be16(buf + 1);
        m.timestamp_ns = read_be48(buf + 5);
        m.order_ref = read_be64(buf + 11);
        return m;
    }
};

struct OrderReplace {
    std::uint16_t stock_locate;
    std::uint64_t timestamp_ns;
    std::uint64_t original_order_ref;
    std::uint64_t new_order_ref;
    std::uint32_t shares;
    std::int64_t  price;

    static OrderReplace decode(const std::uint8_t* buf) {
        // Offset: 0=type(1), 1=stock_locate(2), 3=tracking(2), 5=timestamp(6),
        // 11=original_order_ref(8), 19=new_order_ref(8), 27=shares(4), 31=price(4)
        OrderReplace m;
        m.stock_locate = read_be16(buf + 1);
        m.timestamp_ns = read_be48(buf + 5);
        m.original_order_ref = read_be64(buf + 11);
        m.new_order_ref = read_be64(buf + 19);
        m.shares = read_be32(buf + 27);
        m.price = static_cast<std::int64_t>(read_be32(buf + 31));
        return m;
    }
};

} // namespace ob::itch
