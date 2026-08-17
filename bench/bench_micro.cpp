#include <benchmark/benchmark.h>
#include <ob/order_book.hpp>

// Helper: build a book with N price levels, M orders per level, on one side.
static ob::OrderBook build_book(ob::Side side, int levels, int orders_per_level,
                                ob::Price base_price, ob::Price tick) {
    ob::OrderBook book;
    ob::OrderId id = 1;
    for (int l = 0; l < levels; ++l) {
        ob::Price price = (side == ob::Side::Buy)
            ? base_price - static_cast<ob::Price>(l) * tick
            : base_price + static_cast<ob::Price>(l) * tick;
        for (int o = 0; o < orders_per_level; ++o) {
            ob::Order order;
            order.order_id = id++;
            order.side     = side;
            order.kind     = ob::OrderKind::Limit;
            order.tif      = ob::TimeInForce::GTC;
            order.price    = price;
            order.quantity = 100;
            book.add(order);
        }
    }
    return book;
}

// ---------- 1. Add resting (no cross) ---------------------------------------
// Adds a buy order well below the best ask. The book grows but no match fires.
static void BM_AddResting(benchmark::State& state) {
    // Pre-build a book with 50 ask levels so there's something on the other side.
    auto book = build_book(ob::Side::Sell, 50, 5, 11000, 100);

    ob::OrderId next_id = 100'000;
    for (auto _ : state) {
        ob::Order order;
        order.order_id = next_id++;
        order.side     = ob::Side::Buy;
        order.kind     = ob::OrderKind::Limit;
        order.tif      = ob::TimeInForce::GTC;
        order.price    = 9000;  // well below best ask (11000)
        order.quantity = 100;
        auto trades = book.add(order);
        benchmark::DoNotOptimize(trades);
        benchmark::ClobberMemory();

        // Cancel the order so the book doesn't grow unboundedly
        book.cancel(order.order_id);
    }
}
BENCHMARK(BM_AddResting);

// ---------- 2. Cancel via O(1) index ----------------------------------------
// Pre-add an order, then benchmark the cancel path.
static void BM_Cancel(benchmark::State& state) {
    auto book = build_book(ob::Side::Buy, 50, 10, 10000, 100);

    ob::OrderId next_id = 100'000;
    for (auto _ : state) {
        // Add an order, then cancel it (measures the cancel hot path).
        ob::Order order;
        order.order_id = next_id++;
        order.side     = ob::Side::Buy;
        order.kind     = ob::OrderKind::Limit;
        order.tif      = ob::TimeInForce::GTC;
        order.price    = 10000;  // best bid level
        order.quantity = 100;
        book.add(order);

        bool ok = book.cancel(order.order_id);
        benchmark::DoNotOptimize(ok);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Cancel);

// ---------- 3. Single-level cross match -------------------------------------
// Aggressive sell crosses exactly one resting buy order.
static void BM_MatchSingleLevel(benchmark::State& state) {
    ob::OrderId next_id = 100'000;
    for (auto _ : state) {
        state.PauseTiming();
        ob::OrderBook book;
        // Place one resting buy
        ob::Order rest;
        rest.order_id = next_id++;
        rest.side     = ob::Side::Buy;
        rest.kind     = ob::OrderKind::Limit;
        rest.tif      = ob::TimeInForce::GTC;
        rest.price    = 10000;
        rest.quantity = 100;
        book.add(rest);
        state.ResumeTiming();

        // Aggressive sell crosses it
        ob::Order aggressor;
        aggressor.order_id = next_id++;
        aggressor.side     = ob::Side::Sell;
        aggressor.kind     = ob::OrderKind::Limit;
        aggressor.tif      = ob::TimeInForce::GTC;
        aggressor.price    = 10000;
        aggressor.quantity = 100;
        auto trades = book.add(aggressor);
        benchmark::DoNotOptimize(trades);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_MatchSingleLevel);

// ---------- 4. Multi-level walk match ---------------------------------------
// Aggressive market sell walks through multiple bid levels.
static void BM_MatchMultiLevel(benchmark::State& state) {
    const int levels = 10;
    const int orders_per_level = 5;
    ob::OrderId next_id = 1'000'000;

    for (auto _ : state) {
        state.PauseTiming();
        auto book = build_book(ob::Side::Buy, levels, orders_per_level, 10000, 100);
        state.ResumeTiming();

        // Market sell that walks through all levels
        ob::Order aggressor;
        aggressor.order_id = next_id++;
        aggressor.side     = ob::Side::Sell;
        aggressor.kind     = ob::OrderKind::Market;
        aggressor.tif      = ob::TimeInForce::GTC;
        aggressor.price    = 0;
        aggressor.quantity = static_cast<ob::Qty>(levels * orders_per_level * 100);
        auto trades = book.add(aggressor);
        benchmark::DoNotOptimize(trades);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_MatchMultiLevel);
