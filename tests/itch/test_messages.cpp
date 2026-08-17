#include <gtest/gtest.h>
#include <ob/itch/messages.hpp>

#include <cstring>
#include <vector>

using namespace ob::itch;

namespace {

// Helper to build a big-endian byte buffer
class BEBuilder {
public:
    void put8(std::uint8_t v) { buf_.push_back(v); }
    void put16(std::uint16_t v) {
        buf_.push_back(static_cast<std::uint8_t>(v >> 8));
        buf_.push_back(static_cast<std::uint8_t>(v));
    }
    void put32(std::uint32_t v) {
        buf_.push_back(static_cast<std::uint8_t>(v >> 24));
        buf_.push_back(static_cast<std::uint8_t>(v >> 16));
        buf_.push_back(static_cast<std::uint8_t>(v >> 8));
        buf_.push_back(static_cast<std::uint8_t>(v));
    }
    void put48(std::uint64_t v) {
        buf_.push_back(static_cast<std::uint8_t>(v >> 40));
        buf_.push_back(static_cast<std::uint8_t>(v >> 32));
        buf_.push_back(static_cast<std::uint8_t>(v >> 24));
        buf_.push_back(static_cast<std::uint8_t>(v >> 16));
        buf_.push_back(static_cast<std::uint8_t>(v >> 8));
        buf_.push_back(static_cast<std::uint8_t>(v));
    }
    void put64(std::uint64_t v) {
        buf_.push_back(static_cast<std::uint8_t>(v >> 56));
        buf_.push_back(static_cast<std::uint8_t>(v >> 48));
        buf_.push_back(static_cast<std::uint8_t>(v >> 40));
        buf_.push_back(static_cast<std::uint8_t>(v >> 32));
        buf_.push_back(static_cast<std::uint8_t>(v >> 24));
        buf_.push_back(static_cast<std::uint8_t>(v >> 16));
        buf_.push_back(static_cast<std::uint8_t>(v >> 8));
        buf_.push_back(static_cast<std::uint8_t>(v));
    }
    void putStr(const char* s, size_t len) {
        for (size_t i = 0; i < len; ++i)
            buf_.push_back(i < std::strlen(s) ? static_cast<std::uint8_t>(s[i]) : ' ');
    }
    const std::uint8_t* data() const { return buf_.data(); }
    size_t size() const { return buf_.size(); }
private:
    std::vector<std::uint8_t> buf_;
};

} // namespace

TEST(IchEndian, ReadBE16) {
    std::uint8_t buf[] = {0x01, 0x02};
    EXPECT_EQ(read_be16(buf), 0x0102u);
}

TEST(IchEndian, ReadBE32) {
    std::uint8_t buf[] = {0xDE, 0xAD, 0xBE, 0xEF};
    EXPECT_EQ(read_be32(buf), 0xDEADBEEFu);
}

TEST(IchEndian, ReadBE48) {
    std::uint8_t buf[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    EXPECT_EQ(read_be48(buf), 0x000102030405ULL);
}

TEST(IchEndian, ReadBE64) {
    std::uint8_t buf[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    EXPECT_EQ(read_be64(buf), 0x0123456789ABCDEFULL);
}

TEST(ItchMsg, AddOrder) {
    BEBuilder b;
    b.put8('A');                         // type
    b.put16(42);                         // stock_locate
    b.put16(0);                          // tracking
    b.put48(1234567890ULL);              // timestamp
    b.put64(999);                        // order_ref
    b.put8('B');                         // buy_sell
    b.put32(100);                        // shares
    b.putStr("MSFT", 8);                 // stock
    b.put32(1399500);                    // price = $139.9500

    auto m = AddOrder::decode(b.data());
    EXPECT_EQ(m.stock_locate, 42u);
    EXPECT_EQ(m.timestamp_ns, 1234567890ULL);
    EXPECT_EQ(m.order_ref, 999u);
    EXPECT_EQ(m.buy_sell, 'B');
    EXPECT_EQ(m.shares, 100u);
    EXPECT_EQ(m.price, 1399500);  // 1e-4 scale, raw integer
    EXPECT_EQ(std::string(m.stock.data(), 4), "MSFT");
}

TEST(ItchMsg, AddOrderMPID) {
    BEBuilder b;
    b.put8('F');                         // type
    b.put16(42);                         // stock_locate
    b.put16(0);                          // tracking
    b.put48(1234567890ULL);              // timestamp
    b.put64(888);                        // order_ref
    b.put8('S');                         // buy_sell
    b.put32(200);                        // shares
    b.putStr("AAPL", 8);                 // stock
    b.put32(2025000);                    // price = $202.5000
    b.putStr("NSDQ", 4);                // mpid

    auto m = AddOrderMPID::decode(b.data());
    EXPECT_EQ(m.order_ref, 888u);
    EXPECT_EQ(m.buy_sell, 'S');
    EXPECT_EQ(m.shares, 200u);
    EXPECT_EQ(m.price, 2025000);
    EXPECT_EQ(std::string(m.mpid.data(), 4), "NSDQ");
}

TEST(ItchMsg, OrderExecuted) {
    BEBuilder b;
    b.put8('E');                         // type
    b.put16(42);                         // stock_locate
    b.put16(0);                          // tracking
    b.put48(5555555555ULL);              // timestamp
    b.put64(999);                        // order_ref
    b.put32(50);                         // shares
    b.put64(12345);                      // match_number

    auto m = OrderExecuted::decode(b.data());
    EXPECT_EQ(m.stock_locate, 42u);
    EXPECT_EQ(m.timestamp_ns, 5555555555ULL);
    EXPECT_EQ(m.order_ref, 999u);
    EXPECT_EQ(m.shares, 50u);
    EXPECT_EQ(m.match_number, 12345u);
}

TEST(ItchMsg, OrderExecutedWithPrice) {
    BEBuilder b;
    b.put8('C');                         // type
    b.put16(42);                         // stock_locate
    b.put16(0);                          // tracking
    b.put48(7777777777ULL);              // timestamp
    b.put64(999);                        // order_ref
    b.put32(25);                         // shares
    b.put64(67890);                      // match_number
    b.put8('Y');                         // printable
    b.put32(1400000);                    // price = $140.0000

    auto m = OrderExecutedWithPrice::decode(b.data());
    EXPECT_EQ(m.order_ref, 999u);
    EXPECT_EQ(m.shares, 25u);
    EXPECT_EQ(m.match_number, 67890u);
    EXPECT_EQ(m.printable, 'Y');
    EXPECT_EQ(m.price, 1400000);
}

TEST(ItchMsg, OrderCancel) {
    BEBuilder b;
    b.put8('X');                         // type
    b.put16(42);                         // stock_locate
    b.put16(0);                          // tracking
    b.put48(9999999999ULL);              // timestamp
    b.put64(999);                        // order_ref
    b.put32(30);                         // cancelled_shares

    auto m = OrderCancel::decode(b.data());
    EXPECT_EQ(m.order_ref, 999u);
    EXPECT_EQ(m.cancelled_shares, 30u);
}

TEST(ItchMsg, OrderDelete) {
    BEBuilder b;
    b.put8('D');                         // type
    b.put16(42);                         // stock_locate
    b.put16(0);                          // tracking
    b.put48(1111111111ULL);              // timestamp
    b.put64(999);                        // order_ref

    auto m = OrderDelete::decode(b.data());
    EXPECT_EQ(m.stock_locate, 42u);
    EXPECT_EQ(m.timestamp_ns, 1111111111ULL);
    EXPECT_EQ(m.order_ref, 999u);
}

TEST(ItchMsg, OrderReplace) {
    BEBuilder b;
    b.put8('U');                         // type
    b.put16(42);                         // stock_locate
    b.put16(0);                          // tracking
    b.put48(2222222222ULL);              // timestamp
    b.put64(999);                        // original_order_ref
    b.put64(1000);                       // new_order_ref
    b.put32(75);                         // shares
    b.put32(1410000);                    // price = $141.0000

    auto m = OrderReplace::decode(b.data());
    EXPECT_EQ(m.original_order_ref, 999u);
    EXPECT_EQ(m.new_order_ref, 1000u);
    EXPECT_EQ(m.shares, 75u);
    EXPECT_EQ(m.price, 1410000);
}

TEST(ItchMsg, SystemEvent) {
    BEBuilder b;
    b.put8('S');                         // type
    b.put16(0);                          // stock_locate
    b.put16(0);                          // tracking
    b.put48(3333333333ULL);              // timestamp
    b.put8('Q');                         // event_code (market open)

    auto m = SystemEvent::decode(b.data());
    EXPECT_EQ(m.timestamp_ns, 3333333333ULL);
    EXPECT_EQ(m.event_code, 'Q');
}

TEST(ItchMsg, StockDirectory) {
    BEBuilder b;
    b.put8('R');                         // type
    b.put16(42);                         // stock_locate
    b.put16(0);                          // tracking
    b.put48(4444444444ULL);              // timestamp
    b.putStr("MSFT", 8);                 // stock
    b.put8('Q');                         // market_category
    b.put8('N');                         // financial_status

    auto m = StockDirectory::decode(b.data());
    EXPECT_EQ(m.timestamp_ns, 4444444444ULL);
    EXPECT_EQ(m.symbol(), "MSFT");
    EXPECT_EQ(m.market_category, 'Q');
    EXPECT_EQ(m.financial_status, 'N');
}
