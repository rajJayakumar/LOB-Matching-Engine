// Full-session replay benchmark: drives the engine from Databento MBO or ITCH
// data, times each engine operation individually, and reports per-op latency
// (p50/p99/p99.9) and throughput (messages/wall-time).
//
// Usage:
//   bench_replay --source=databento --data=data/databento/xnas-itch-20250113.mbo.dbn.zst
//   bench_replay --source=itch --symbol=AAPL --data=data/itch/08302019.NASDAQ_ITCH50

#include <ob/dbn/loader.hpp>
#include <ob/itch/parser.hpp>
#include <ob/itch/replay.hpp>
#include <ob/latency_histogram.hpp>
#include <ob/order_book.hpp>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

// Standalone DoNotOptimize / ClobberMemory (bench_replay has its own main,
// no link to Google Benchmark library).
namespace benchmark {
template <class Tp>
inline void DoNotOptimize(Tp const& value) {
    asm volatile("" : : "r,m"(value) : "memory");
}
inline void ClobberMemory() {
    asm volatile("" : : : "memory");
}
} // namespace benchmark

namespace {

constexpr std::uint8_t FLAG_LAST = 0x80;

// ---------- Databento MBO replay with per-op timing -------------------------

struct DbnReplayResult {
    ob::LatencyHistogram add_hist;
    ob::LatencyHistogram cancel_hist;
    ob::LatencyHistogram modify_hist;
    ob::LatencyHistogram total_hist;  // all ops combined
    std::size_t event_count = 0;
    double wall_seconds = 0.0;
};

DbnReplayResult run_databento(const std::string& path) {
    auto events = ob::dbn::load_mbo(path);

    DbnReplayResult result;
    result.event_count = events.size();

    // Databento nanodollar tick = $0.001 = 1M nanodollars (handles sub-penny midpoints)
    ob::dbn::BookBuilder builder(1'000'000, 65536);
    auto timer_overhead = ob::SteadyTimer::measure_overhead();

    auto wall_start = std::chrono::steady_clock::now();

    for (const auto& evt : events) {
        auto t0 = ob::SteadyTimer::now_ns();
        auto trades = builder.apply(evt);
        benchmark::DoNotOptimize(trades);
        auto t1 = ob::SteadyTimer::now_ns();

        std::uint64_t elapsed = (t1 > t0 + timer_overhead) ? (t1 - t0 - timer_overhead) : 0;
        result.total_hist.record(elapsed);

        using Action = ob::dbn::Action;
        switch (evt.action) {
            case Action::Add:
                result.add_hist.record(elapsed);
                break;
            case Action::Cancel:
                result.cancel_hist.record(elapsed);
                break;
            case Action::Modify:
                result.modify_hist.record(elapsed);
                break;
            default:
                break;
        }
    }

    auto wall_end = std::chrono::steady_clock::now();
    result.wall_seconds = std::chrono::duration<double>(wall_end - wall_start).count();
    return result;
}

// ---------- ITCH replay with per-op timing ----------------------------------

// Timed replay handler: wraps ReplayHandler to measure per-op latency.
class TimedItchHandler : public ob::itch::Handler {
public:
    explicit TimedItchHandler(const std::string& symbol)
        : inner_(symbol, 100)  // ITCH penny tick = 100 (1e-4 units)
        , timer_overhead_(ob::SteadyTimer::measure_overhead()) {}

    ob::LatencyHistogram add_hist;
    ob::LatencyHistogram cancel_hist;
    ob::LatencyHistogram modify_hist;
    ob::LatencyHistogram total_hist;

    std::size_t events_applied() const { return inner_.events_applied(); }
    const ob::OrderBook& book() const { return inner_.book(); }

    void on_stock_directory(const ob::itch::StockDirectory& m) override {
        inner_.on_stock_directory(m);
    }

    void on_add_order(const ob::itch::AddOrder& m) override {
        auto t0 = ob::SteadyTimer::now_ns();
        inner_.on_add_order(m);
        auto t1 = ob::SteadyTimer::now_ns();
        record(t0, t1, add_hist);
    }

    void on_add_order_mpid(const ob::itch::AddOrderMPID& m) override {
        auto t0 = ob::SteadyTimer::now_ns();
        inner_.on_add_order_mpid(m);
        auto t1 = ob::SteadyTimer::now_ns();
        record(t0, t1, add_hist);
    }

    void on_order_executed(const ob::itch::OrderExecuted& m) override {
        auto t0 = ob::SteadyTimer::now_ns();
        inner_.on_order_executed(m);
        auto t1 = ob::SteadyTimer::now_ns();
        record(t0, t1, cancel_hist);  // reduce = partial cancel
    }

    void on_order_executed_wp(const ob::itch::OrderExecutedWithPrice& m) override {
        auto t0 = ob::SteadyTimer::now_ns();
        inner_.on_order_executed_wp(m);
        auto t1 = ob::SteadyTimer::now_ns();
        record(t0, t1, cancel_hist);
    }

    void on_order_cancel(const ob::itch::OrderCancel& m) override {
        auto t0 = ob::SteadyTimer::now_ns();
        inner_.on_order_cancel(m);
        auto t1 = ob::SteadyTimer::now_ns();
        record(t0, t1, cancel_hist);
    }

    void on_order_delete(const ob::itch::OrderDelete& m) override {
        auto t0 = ob::SteadyTimer::now_ns();
        inner_.on_order_delete(m);
        auto t1 = ob::SteadyTimer::now_ns();
        record(t0, t1, cancel_hist);
    }

    void on_order_replace(const ob::itch::OrderReplace& m) override {
        auto t0 = ob::SteadyTimer::now_ns();
        inner_.on_order_replace(m);
        auto t1 = ob::SteadyTimer::now_ns();
        record(t0, t1, modify_hist);
    }

private:
    void record(std::uint64_t t0, std::uint64_t t1, ob::LatencyHistogram& hist) {
        std::uint64_t elapsed = (t1 > t0 + timer_overhead_) ? (t1 - t0 - timer_overhead_) : 0;
        total_hist.record(elapsed);
        hist.record(elapsed);
    }

    ob::itch::ReplayHandler inner_;
    std::uint64_t timer_overhead_;
};

struct ItchReplayResult {
    ob::LatencyHistogram add_hist;
    ob::LatencyHistogram cancel_hist;
    ob::LatencyHistogram modify_hist;
    ob::LatencyHistogram total_hist;
    std::size_t event_count = 0;
    double wall_seconds = 0.0;
};

ItchReplayResult run_itch(const std::string& path, const std::string& symbol) {
    TimedItchHandler handler(symbol);

    auto wall_start = std::chrono::steady_clock::now();
    ob::itch::parse_file(path, handler);
    auto wall_end = std::chrono::steady_clock::now();

    ItchReplayResult result;
    result.add_hist    = handler.add_hist;
    result.cancel_hist = handler.cancel_hist;
    result.modify_hist = handler.modify_hist;
    result.total_hist  = handler.total_hist;
    result.event_count = handler.events_applied();
    result.wall_seconds = std::chrono::duration<double>(wall_end - wall_start).count();
    return result;
}

// ---------- Reporting -------------------------------------------------------

void print_histogram(const char* label, const ob::LatencyHistogram& h) {
    if (h.count() == 0) {
        std::cout << "  " << std::setw(10) << label << ":  (no samples)\n";
        return;
    }
    std::cout << "  " << std::setw(10) << label << ":"
              << "  count=" << std::setw(10) << h.count()
              << "  p50=" << std::setw(6) << h.p50() << " ns"
              << "  p99=" << std::setw(6) << h.p99() << " ns"
              << "  p99.9=" << std::setw(6) << h.p999() << " ns\n";
}

void print_results(const char* source,
                   std::size_t event_count, double wall_seconds,
                   const ob::LatencyHistogram& total,
                   const ob::LatencyHistogram& add,
                   const ob::LatencyHistogram& cancel,
                   const ob::LatencyHistogram& modify) {
    double throughput = (wall_seconds > 0.0)
        ? static_cast<double>(event_count) / wall_seconds
        : 0.0;

    std::cout << "\n=== Replay benchmark: " << source << " ===\n";
    std::cout << "  Events:     " << event_count << "\n";
    std::cout << "  Wall time:  " << std::fixed << std::setprecision(3) << wall_seconds << " s\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << throughput << " msg/s\n\n";
    std::cout << "  Per-op latency (timer overhead subtracted):\n";

    print_histogram("total", total);
    print_histogram("add", add);
    print_histogram("cancel", cancel);
    print_histogram("modify", modify);

    auto timer_oh = ob::SteadyTimer::measure_overhead();
    std::cout << "\n  Timer overhead (median): " << timer_oh << " ns\n";
}

std::string get_arg(int argc, char* argv[], const char* prefix) {
    std::size_t plen = std::strlen(prefix);
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], prefix, plen) == 0) {
            return std::string(argv[i] + plen);
        }
    }
    return "";
}

} // namespace

int main(int argc, char* argv[]) {
    std::string source = get_arg(argc, argv, "--source=");
    std::string data   = get_arg(argc, argv, "--data=");
    std::string symbol = get_arg(argc, argv, "--symbol=");

    if (source.empty() || data.empty()) {
        std::cerr << "Usage:\n"
                  << "  bench_replay --source=databento --data=<mbo.dbn.zst>\n"
                  << "  bench_replay --source=itch --symbol=AAPL --data=<itch_file>\n";
        return 1;
    }

    if (!std::filesystem::exists(data)) {
        std::cout << "SKIPPED: data file not found: " << data << "\n";
        return 0;
    }

    if (source == "databento") {
        auto r = run_databento(data);
        print_results("Databento MBO", r.event_count, r.wall_seconds,
                      r.total_hist, r.add_hist, r.cancel_hist, r.modify_hist);
    } else if (source == "itch") {
        if (symbol.empty()) {
            std::cerr << "Error: --symbol required for ITCH source\n";
            return 1;
        }
        auto r = run_itch(data, symbol);
        print_results("ITCH", r.event_count, r.wall_seconds,
                      r.total_hist, r.add_hist, r.cancel_hist, r.modify_hist);
    } else {
        std::cerr << "Unknown source: " << source << ". Use 'databento' or 'itch'.\n";
        return 1;
    }

    return 0;
}
