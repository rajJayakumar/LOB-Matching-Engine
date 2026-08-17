#pragma once

// Fixed-bucket latency histogram for sub-microsecond per-op timing.
// Accumulates samples into pre-sized buckets; no per-sample heap allocation.
// Reports p50 / p99 / p99.9 in nanoseconds.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace ob {

class LatencyHistogram {
public:
    // Each bucket = 1 ns.  Max trackable = MAX_NS - 1 ns.
    // Anything >= MAX_NS is clamped into the last bucket.
    static constexpr std::size_t MAX_NS = 100'000;  // 100 us

    void record(std::uint64_t ns) {
        if (ns >= MAX_NS) ns = MAX_NS - 1;
        ++buckets_[ns];
        ++count_;
    }

    std::uint64_t count() const { return count_; }

    // Return the value at the given percentile (0.0–100.0).
    std::uint64_t percentile(double pct) const {
        if (count_ == 0) return 0;
        std::uint64_t target = static_cast<std::uint64_t>(
            static_cast<double>(count_) * pct / 100.0);
        if (target == 0) target = 1;
        std::uint64_t cumulative = 0;
        for (std::size_t i = 0; i < MAX_NS; ++i) {
            cumulative += buckets_[i];
            if (cumulative >= target) return i;
        }
        return MAX_NS - 1;
    }

    std::uint64_t p50()  const { return percentile(50.0); }
    std::uint64_t p99()  const { return percentile(99.0); }
    std::uint64_t p999() const { return percentile(99.9); }

    void reset() {
        buckets_.fill(0);
        count_ = 0;
    }

private:
    std::array<std::uint64_t, MAX_NS> buckets_{};
    std::uint64_t count_ = 0;
};

// Monotonic high-resolution timer for per-op measurement.
// Uses clock_gettime(CLOCK_MONOTONIC) — portable across macOS and Linux.
struct SteadyTimer {
    using clock = std::chrono::steady_clock;

    static std::uint64_t now_ns() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                clock::now().time_since_epoch())
                .count());
    }

    // Measure the timer's own call overhead (median of N round-trips).
    static std::uint64_t measure_overhead(std::size_t iterations = 10'000) {
        std::vector<std::uint64_t> samples;
        samples.reserve(iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto t0 = now_ns();
            auto t1 = now_ns();
            samples.push_back(t1 - t0);
        }
        std::sort(samples.begin(), samples.end());
        return samples[samples.size() / 2];  // median
    }
};

} // namespace ob
