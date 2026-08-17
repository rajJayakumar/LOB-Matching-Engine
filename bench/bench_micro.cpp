#include <benchmark/benchmark.h>
#include <ob/order_book.hpp>

// Trivial sanity benchmark — replaced with real hot-path cases in Task 3.3.
static void BM_AddResting(benchmark::State& state) {
    ob::OrderBook book;
    ob::OrderId next_id = 1;
    for (auto _ : state) {
        ob::Order order;
        order.order_id = next_id++;
        order.side     = ob::Side::Buy;
        order.kind     = ob::OrderKind::Limit;
        order.tif      = ob::TimeInForce::GTC;
        order.price    = 10000;
        order.quantity = 100;
        book.add(order);
        benchmark::DoNotOptimize(book);
    }
}
BENCHMARK(BM_AddResting);
