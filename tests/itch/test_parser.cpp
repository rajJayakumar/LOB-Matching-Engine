#include <gtest/gtest.h>
#include <ob/itch/parser.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ob::itch;

namespace {

// Helper to write big-endian values to a buffer
class BEWriter {
public:
    void put8(std::uint8_t v) { buf_.push_back(v); }
    void put16(std::uint16_t v) {
        buf_.push_back(static_cast<std::uint8_t>(v >> 8));
        buf_.push_back(static_cast<std::uint8_t>(v));
    }
    void put32(std::uint32_t v) {
        for (int i = 24; i >= 0; i -= 8)
            buf_.push_back(static_cast<std::uint8_t>(v >> i));
    }
    void put48(std::uint64_t v) {
        for (int i = 40; i >= 0; i -= 8)
            buf_.push_back(static_cast<std::uint8_t>(v >> i));
    }
    void put64(std::uint64_t v) {
        for (int i = 56; i >= 0; i -= 8)
            buf_.push_back(static_cast<std::uint8_t>(v >> i));
    }
    void putStr(const char* s, size_t len) {
        for (size_t i = 0; i < len; ++i)
            buf_.push_back(i < std::strlen(s) ? static_cast<std::uint8_t>(s[i]) : ' ');
    }

    // Write a length-prefixed message: 2-byte BE length + body
    void beginMsg(size_t body_size) {
        put16(static_cast<std::uint16_t>(body_size));
    }

    const std::uint8_t* data() const { return buf_.data(); }
    size_t size() const { return buf_.size(); }
    void clear() { buf_.clear(); }

    // Write the buffer to a temporary file and return the path
    std::string write_to_tmp() const {
        std::string path = std::tmpnam(nullptr);
        FILE* fp = std::fopen(path.c_str(), "wb");
        std::fwrite(buf_.data(), 1, buf_.size(), fp);
        std::fclose(fp);
        return path;
    }
private:
    std::vector<std::uint8_t> buf_;
};

// Tracking handler to verify dispatch order and payloads
struct TrackingHandler : Handler {
    std::vector<std::string> events;
    AddOrder last_add{};
    OrderExecuted last_exec{};
    OrderCancel last_cancel{};
    OrderDelete last_delete{};
    OrderReplace last_replace{};

    void on_system_event(const SystemEvent& m) override {
        events.push_back(std::string("S:") + m.event_code);
    }
    void on_stock_directory(const StockDirectory& m) override {
        events.push_back("R:" + m.symbol());
    }
    void on_add_order(const AddOrder& m) override {
        events.push_back("A:" + std::to_string(m.order_ref));
        last_add = m;
    }
    void on_add_order_mpid(const AddOrderMPID& m) override {
        events.push_back("F:" + std::to_string(m.order_ref));
    }
    void on_order_executed(const OrderExecuted& m) override {
        events.push_back("E:" + std::to_string(m.order_ref));
        last_exec = m;
    }
    void on_order_executed_wp(const OrderExecutedWithPrice& m) override {
        events.push_back("C:" + std::to_string(m.order_ref));
    }
    void on_order_cancel(const OrderCancel& m) override {
        events.push_back("X:" + std::to_string(m.order_ref));
        last_cancel = m;
    }
    void on_order_delete(const OrderDelete& m) override {
        events.push_back("D:" + std::to_string(m.order_ref));
        last_delete = m;
    }
    void on_order_replace(const OrderReplace& m) override {
        events.push_back("U:" + std::to_string(m.original_order_ref));
        last_replace = m;
    }
};

// Build a synthetic ITCH stream with a sequence of messages
std::string build_test_stream() {
    BEWriter w;

    // Message 1: System Event 'S' (12 bytes body)
    w.beginMsg(12);
    w.put8('S');           // type
    w.put16(0);            // stock_locate
    w.put16(0);            // tracking
    w.put48(1000000000);   // timestamp
    w.put8('O');           // event_code = start of messages

    // Message 2: Stock Directory 'R' (39 bytes body)
    w.beginMsg(39);
    w.put8('R');           // type
    w.put16(1);            // stock_locate
    w.put16(0);            // tracking
    w.put48(2000000000);   // timestamp
    w.putStr("MSFT", 8);   // stock
    w.put8('Q');           // market_category
    w.put8('N');           // financial_status
    // Remaining fields (18 bytes of misc data)
    for (int i = 0; i < 18; ++i) w.put8(0);

    // Message 3: Add Order 'A' (36 bytes body)
    w.beginMsg(36);
    w.put8('A');           // type
    w.put16(1);            // stock_locate
    w.put16(0);            // tracking
    w.put48(3000000000);   // timestamp
    w.put64(100);          // order_ref
    w.put8('B');           // buy_sell
    w.put32(500);          // shares
    w.putStr("MSFT", 8);   // stock
    w.put32(1399500);      // price = $139.95

    // Message 4: Order Executed 'E' (31 bytes body)
    w.beginMsg(31);
    w.put8('E');           // type
    w.put16(1);            // stock_locate
    w.put16(0);            // tracking
    w.put48(4000000000);   // timestamp
    w.put64(100);          // order_ref
    w.put32(200);          // shares
    w.put64(1);            // match_number

    // Message 5: Order Cancel 'X' (23 bytes body)
    w.beginMsg(23);
    w.put8('X');           // type
    w.put16(1);            // stock_locate
    w.put16(0);            // tracking
    w.put48(5000000000);   // timestamp
    w.put64(100);          // order_ref
    w.put32(100);          // cancelled_shares

    // Message 6: Order Delete 'D' (19 bytes body)
    w.beginMsg(19);
    w.put8('D');           // type
    w.put16(1);            // stock_locate
    w.put16(0);            // tracking
    w.put48(6000000000);   // timestamp
    w.put64(100);          // order_ref

    // Message 7: Order Replace 'U' (35 bytes body)
    w.beginMsg(35);
    w.put8('U');           // type
    w.put16(1);            // stock_locate
    w.put16(0);            // tracking
    w.put48(7000000000);   // timestamp
    w.put64(100);          // original_order_ref
    w.put64(101);          // new_order_ref
    w.put32(300);          // shares
    w.put32(1405000);      // price = $140.50

    return w.write_to_tmp();
}

} // namespace

TEST(ItchParser, SyntheticStreamDispatch) {
    auto path = build_test_stream();
    TrackingHandler h;
    auto count = parse_file(path, h);
    std::remove(path.c_str());

    EXPECT_EQ(count, 7u);
    ASSERT_EQ(h.events.size(), 7u);
    EXPECT_EQ(h.events[0], "S:O");
    EXPECT_EQ(h.events[1], "R:MSFT");
    EXPECT_EQ(h.events[2], "A:100");
    EXPECT_EQ(h.events[3], "E:100");
    EXPECT_EQ(h.events[4], "X:100");
    EXPECT_EQ(h.events[5], "D:100");
    EXPECT_EQ(h.events[6], "U:100");
}

TEST(ItchParser, DecodedPayloads) {
    auto path = build_test_stream();
    TrackingHandler h;
    parse_file(path, h);
    std::remove(path.c_str());

    // Verify Add payload
    EXPECT_EQ(h.last_add.order_ref, 100u);
    EXPECT_EQ(h.last_add.buy_sell, 'B');
    EXPECT_EQ(h.last_add.shares, 500u);
    EXPECT_EQ(h.last_add.price, 1399500);
    EXPECT_EQ(h.last_add.timestamp_ns, 3000000000ULL);

    // Verify Execute payload
    EXPECT_EQ(h.last_exec.order_ref, 100u);
    EXPECT_EQ(h.last_exec.shares, 200u);

    // Verify Cancel payload
    EXPECT_EQ(h.last_cancel.order_ref, 100u);
    EXPECT_EQ(h.last_cancel.cancelled_shares, 100u);

    // Verify Delete payload
    EXPECT_EQ(h.last_delete.order_ref, 100u);

    // Verify Replace payload
    EXPECT_EQ(h.last_replace.original_order_ref, 100u);
    EXPECT_EQ(h.last_replace.new_order_ref, 101u);
    EXPECT_EQ(h.last_replace.shares, 300u);
    EXPECT_EQ(h.last_replace.price, 1405000);
}

TEST(ItchParser, EmptyFile) {
    BEWriter w;
    auto path = w.write_to_tmp();
    TrackingHandler h;
    auto count = parse_file(path, h);
    std::remove(path.c_str());

    EXPECT_EQ(count, 0u);
    EXPECT_TRUE(h.events.empty());
}
