#pragma once

// ITCH 5.0 binary-file parser: reads 2-byte BE length prefix, dispatches on
// message type byte, and invokes typed callbacks on a visitor/handler.

#include <ob/itch/messages.hpp>

#include <cstdint>
#include <string>

namespace ob::itch {

// Handler interface — override the callbacks you care about.
// Default implementations are no-ops.
struct Handler {
    virtual ~Handler() = default;
    virtual void on_system_event(const SystemEvent&) {}
    virtual void on_stock_directory(const StockDirectory&) {}
    virtual void on_add_order(const AddOrder&) {}
    virtual void on_add_order_mpid(const AddOrderMPID&) {}
    virtual void on_order_executed(const OrderExecuted&) {}
    virtual void on_order_executed_wp(const OrderExecutedWithPrice&) {}
    virtual void on_order_cancel(const OrderCancel&) {}
    virtual void on_order_delete(const OrderDelete&) {}
    virtual void on_order_replace(const OrderReplace&) {}
};

// Parse an ITCH 5.0 binary file (BinaryFILE format with 2-byte BE length
// prefix per message). Streams from disk without loading the whole file.
// Returns the total number of messages processed.
std::size_t parse_file(const std::string& path, Handler& handler);

} // namespace ob::itch
