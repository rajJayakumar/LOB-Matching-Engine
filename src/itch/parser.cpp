#include <ob/itch/parser.hpp>

#include <cstdio>
#include <stdexcept>
#include <vector>

namespace ob::itch {

std::size_t parse_file(const std::string& path, Handler& handler) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        throw std::runtime_error("Cannot open ITCH file: " + path);
    }

    std::vector<std::uint8_t> buf(65536);  // Max ITCH message is well under 64KB
    std::size_t count = 0;
    std::uint8_t len_buf[2];

    while (std::fread(len_buf, 1, 2, fp) == 2) {
        std::uint16_t msg_len = read_be16(len_buf);
        if (msg_len == 0) continue;
        if (msg_len > buf.size()) buf.resize(msg_len);

        if (std::fread(buf.data(), 1, msg_len, fp) != msg_len) break;

        auto type = static_cast<MsgType>(buf[0]);
        switch (type) {
            case MsgType::SystemEvent:
                handler.on_system_event(SystemEvent::decode(buf.data()));
                break;
            case MsgType::StockDirectory:
                handler.on_stock_directory(StockDirectory::decode(buf.data()));
                break;
            case MsgType::AddOrder:
                handler.on_add_order(AddOrder::decode(buf.data()));
                break;
            case MsgType::AddOrderMPID:
                handler.on_add_order_mpid(AddOrderMPID::decode(buf.data()));
                break;
            case MsgType::OrderExecuted:
                handler.on_order_executed(OrderExecuted::decode(buf.data()));
                break;
            case MsgType::OrderExecutedWP:
                handler.on_order_executed_wp(OrderExecutedWithPrice::decode(buf.data()));
                break;
            case MsgType::OrderCancel:
                handler.on_order_cancel(OrderCancel::decode(buf.data()));
                break;
            case MsgType::OrderDelete:
                handler.on_order_delete(OrderDelete::decode(buf.data()));
                break;
            case MsgType::OrderReplace:
                handler.on_order_replace(OrderReplace::decode(buf.data()));
                break;
            default:
                // Skip message types we don't handle (P, Q, B, I, H, Y, L, etc.)
                break;
        }
        ++count;
    }

    std::fclose(fp);
    return count;
}

} // namespace ob::itch
