# Phase 3 Performance Log

## Environment

All measurements are captured on the profiling VM, never on the macOS dev machine.

- **VM:** GCP `c4-standard-4` (x86-64, Intel Granite Rapids, 4 vCPUs @ 2300 MHz)
- **PMU level:** `standard` (core + L1/L2 + branch events; no LLC events)
- **OS:** Debian 13
- **Compiler:** GCC 14.2.0
- **Build flags:** `-O3 -fno-omit-frame-pointer -march=native`
- **`OB_NATIVE_ARCH`:** ON (default for local/VM builds; OFF for portability)
- **CPU caches:** L1d 48 KiB (x2), L1i 32 KiB (x2), L2 2048 KiB (x2), L3 266240 KiB

### VM creation

```bash
gcloud compute instances create orderbook-perf \
  --zone=us-central1-c \
  --machine-type=c4-standard-4 \
  --performance-monitoring-unit=standard \
  --image-family=debian-13 --image-project=debian-cloud \
  --boot-disk-size=50GB --boot-disk-type=hyperdisk-balanced
```

### VM setup

```bash
sudo apt update && sudo apt install -y linux-perf build-essential cmake git libssl-dev libzstd-dev
echo 'kernel.perf_event_paranoid=1' | sudo tee /etc/sysctl.d/99-perf.conf
sudo sysctl kernel.perf_event_paranoid=1
```

### Validated `perf stat` event list

`standard` PMU does **not** expose LLC events. Do not use `perf stat -d` or the generic
`cache-misses` alias (both resolve to the unsupported LLC counter). Use:

```bash
perf stat -e cycles,instructions,branches,branch-misses,L1-dcache-loads,L1-dcache-load-misses <cmd>
```

This yields three key metrics:
- **IPC** = `instructions / cycles`
- **Branch-mispredict rate** = `branch-misses / branches`
- **L1 d-cache miss rate** = `L1-dcache-load-misses / L1-dcache-loads`

### Timer overhead

Median timer overhead on Granite Rapids: **25 ns** (measured via `SteadyTimer::measure_overhead()`).
Subtracted from all per-op timing samples. Timed operations guarded against DCE with
`DoNotOptimize` / `ClobberMemory`.

---

## Baseline (Task 3.4)

### Microbenchmarks (Google Benchmark, median CPU time)

| Benchmark          | Time (ns) | CV   |
|--------------------|-----------|------|
| BM_AddResting      | 61.6      | 0.34% |
| BM_Cancel          | 49.5      | 0.19% |
| BM_MatchSingleLevel | 315      | 0.17% |
| BM_MatchMultiLevel | 2691      | 0.31% |

### INTC replay (534K events, dev case)

| Metric     | p50   | p99   | p99.9  |
|------------|-------|-------|--------|
| Total      | 76 ns | 308 ns | 1672 ns |
| Add        | 89 ns | 369 ns | 1803 ns |
| Cancel     | 77 ns | 267 ns | 429 ns |
| Throughput | 6.56M msg/s | | |

### AAPL replay (3.95M events, headline case)

| Metric     | p50    | p99    | p99.9   |
|------------|--------|--------|---------|
| Total      | 103 ns | 329 ns | 1163 ns |
| Add        | 135 ns | 375 ns | 1619 ns |
| Cancel     | 97 ns  | 258 ns | 502 ns  |
| Throughput | 5.74M msg/s | | |

### Hardware counters (AAPL, `perf stat`)

| Counter               | Value         | Derived            |
|------------------------|---------------|--------------------|
| cycles                 | 3,438,605,779 |                    |
| instructions           | 5,653,477,203 | **IPC = 1.64**     |
| branches               | 1,024,949,889 |                    |
| branch-misses          | 18,192,125    | **1.77%**          |
| L1-dcache-loads        | 1,583,260,378 |                    |
| L1-dcache-load-misses  | 24,271,300    | **1.53%**          |

### Profile hot spots (`perf record` on AAPL replay)

```
57.06%  main (bench_replay event loop)
  41.29%  BookBuilder::apply
    24.09%  OrderBook::add_resting
      22.64%  OrderBook::rest
        14.84%  unordered_map::operator[] (index insert)
         1.17%  unordered_map rehash
    14.74%  OrderBook::reduce
       9.50%  OrderBook::cancel
         2.33%  unordered_map::_M_erase
  13.39%  load_mbo (file I/O + decode, not engine)
17.34%  clock_gettime (timer overhead — in timing loop, not engine)
```

### Profile read

The engine hot path (`apply` = 41.3% of total) breaks down as:

1. **`rest()` = 22.6%** — dominated by `unordered_map::operator[]` for the order-ID index
   insertion (14.8%). The `std::map` level lookup + `std::list::push_back` account for the
   remaining ~7.8%.

2. **`cancel()` via `reduce()` = 14.7%** — `unordered_map::_M_erase` is 2.3%; the remaining
   ~7.2% is `std::map::find` + `std::list::erase` per-node deallocation.

3. **`std::map` (red-black tree) operations total ~15%** across rest + cancel — O(log n) level
   lookup with pointer-chasing cache misses.

4. **`unordered_map` operations total ~17%** — hash-table insert/erase for the order-ID index,
   including occasional rehash (1.2%).

5. **Timer overhead (17.3%)** is significant but does not affect the engine's actual performance
   — it inflates wall time but is subtracted from per-op latency samples.

6. **File I/O + decode (13.4%)** is outside the engine — `load_mbo` page faults dominate.

**Conclusion:** The two main targets are (a) `std::map` level containers (~15%) — replace with
flat tick-indexed array (Task 3.5), and (b) per-order allocation in `std::list` + `unordered_map`
churn (~17%) — address with object pool (Task 3.6) and intrusive lists (Task 3.7). The flat-array
change also eliminates per-level `std::map` node allocation. The profile confirms the planned
optimization order: Task 3.5 (flat array) then 3.6 (pool) then 3.7 (intrusive list).

---

## Task 3.5 — Flat tick-indexed price-level array

Replaced both `std::map<Price, PriceLevel>` sides with flat `std::vector<PriceLevel>`
indexed by `(price - base) / tick_size`. O(1) level lookup vs O(log N). Out-of-band
outlier prices (rare stub quotes) spill to a per-side overflow `std::map`.

### Microbenchmarks (after vs baseline)

| Benchmark          | Baseline (ns) | After (ns) | Delta |
|--------------------|---------------|------------|-------|
| BM_AddResting      | 61.6          | 48.2       | -22%  |
| BM_Cancel          | 49.5          | 47.8       | -3%   |
| BM_MatchSingleLevel | 315          | 266        | -16%  |
| BM_MatchMultiLevel | 2691          | 2107       | -22%  |

### AAPL replay (3.95M events, after vs baseline)

| Metric     | Baseline | After  | Delta  |
|------------|----------|--------|--------|
| p50 total  | 103 ns   | 56 ns  | **-46%** |
| p99 total  | 329 ns   | 269 ns | -18%   |
| p99.9 total| 1163 ns  | 698 ns | -40%   |
| Add p50    | 135 ns   | 99 ns  | -27%   |
| Cancel p50 | 97 ns    | 48 ns  | **-51%** |
| Throughput | 5.74M    | 7.47M  | **+30%** |

### Hardware counters (AAPL, after vs baseline)

| Counter               | Baseline       | After          | Delta |
|------------------------|----------------|----------------|-------|
| IPC                    | 1.64           | 1.77           | +8%   |
| L1-dcache-load-misses  | 1.53%          | 1.42%          | -7%   |
| branch-misses          | 1.77%          | 1.33%          | -25%  |

### Notes

- Band initialized once on first order per side; no rebasing needed — a single-day
  session stays well within the ±$32 band.
- Fixed two bugs during development: (1) sub-penny tick causing index collisions
  (tick 10M→1M), (2) linear scan of empty levels on last-level exhaustion
  (short-circuited with count==0 check).
- The `std::map` overhead (~15% of engine time in baseline profile) is eliminated on
  the hot path; only overflow orders (<<0.1%) still use map operations.

---

## Running numbers log

| Task | Change | p50 (ns) | p99 (ns) | p99.9 (ns) | Throughput (msg/s) | L1 miss % | IPC | Branch miss % | Notes |
|------|--------|----------|----------|------------|--------------------|-----------|----|---------------|-------|
| 3.4  | Baseline (AAPL) | 103 | 329 | 1163 | 5.74M | 1.53% | 1.64 | 1.77% | std::map ~15%, unordered_map ~17% of engine |
| 3.5  | Flat tick-indexed array | 56 | 269 | 698 | 7.47M | 1.42% | 1.77 | 1.33% | p50 -46%, throughput +30%, O(1) level lookup |
