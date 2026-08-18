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

### Post-3.5 profile (`perf record` on AAPL replay)

```
20.55%  __vdso_clock_gettime          (timer overhead — not engine)
15.23%  unordered_map::operator[]     (order-ID index insert)
 7.20%  clear_page_erms               (kernel page zeroing for std::list node alloc)
 4.12%  load_mbo                      (file I/O + decode)
 3.75%  OrderBook::reduce
 3.20%  OrderBook::rest
 3.17%  OrderBook::cancel
 2.81%  BookBuilder::apply
 2.05%  main (timing loop)
 1.58%  cfree                         (std::list node dealloc)
 1.51%  unordered_map erase
 1.45%  DbnDecoder::DecodeRecord
```

**Profile read:** `std::map` overhead is gone (was ~15%, now 0%). The new bottleneck is
(a) `unordered_map` insert/erase for the order-ID index (~16.7% combined), and
(b) per-order `std::list` node allocation/deallocation (~8.8%: `clear_page_erms` 7.2% +
`cfree` 1.6%). Next targets: Task 3.6 (object pool to eliminate per-node alloc/free) and
Task 3.7 (intrusive lists to remove `std::list` entirely).

---

## Task 3.6 — FreeListPool allocator for std::list nodes

Replaced per-node `malloc`/`free` with a free-list object pool (`FreeListPool`)
backed by an STL-compatible `PoolAllocator<T>`. The pool grows in 8192-slot blocks;
O(1) acquire/release in steady state. Used custom allocator instead of
`std::pmr::memory_resource` because Apple Clang's libc++ does not ship `<memory_resource>`.

### Microbenchmarks (after vs Task 3.5)

| Benchmark          | Task 3.5 (ns) | After (ns) | Delta |
|--------------------|---------------|------------|-------|
| BM_AddResting      | 48.2          | 48.2       | 0%    |
| BM_Cancel          | 47.8          | 48.0       | 0%    |
| BM_MatchSingleLevel | 266          | 276        | +4%   |
| BM_MatchMultiLevel | 2107          | 1599       | **-24%** |

### AAPL replay (3.95M events, after vs Task 3.5)

| Metric     | Task 3.5 | After  | Delta  |
|------------|----------|--------|--------|
| p50 total  | 56 ns    | 51 ns  | **-9%**  |
| p99 total  | 269 ns   | 223 ns | **-17%** |
| p99.9 total| 698 ns   | 421 ns | **-40%** |
| Add p50    | 99 ns    | 85 ns  | **-14%** |
| Cancel p50 | 48 ns    | 45 ns  | -6%    |
| Throughput | 7.47M    | 8.15M  | **+9%**  |

### Hardware counters (AAPL, after vs Task 3.5)

| Counter               | Task 3.5       | After          | Delta |
|------------------------|----------------|----------------|-------|
| cycles                 | 3,438,605,779  | 2,909,061,729  | **-15%** |
| instructions           | 5,653,477,203  | 5,037,775,923  | **-11%** |
| IPC                    | 1.77           | 1.73           | -2%   |
| L1-dcache-load-misses  | 1.42%          | 1.33%          | -6%   |
| branch-misses          | 1.33%          | 1.42%          | +7%   |

### Notes

- The p99.9 improvement (-40%) comes from eliminating `clear_page_erms` (kernel page
  zeroing for malloc'd std::list nodes, 7.2% of profile) and `cfree` (1.6%).
- Multi-level match benchmark improved 24% — this exercises many node alloc/dealloc
  cycles as orders are fully filled and erased.
- Single-op microbenchmarks (AddResting, Cancel) are flat because they add+cancel one
  order per iteration and the pool's benefit is amortized over many operations.
- 11% fewer instructions executed — malloc/free call chains eliminated from the hot path.

### Post-3.6 profile (`perf record` on AAPL replay)

```
20.96%  __vdso_clock_gettime          (timer overhead — not engine)
11.76%  unordered_map::operator[]     (order-ID index insert)
 6.97%  clear_page_erms               (kernel page zeroing — now from unordered_map)
 4.79%  OrderBook::rest
 4.72%  OrderBook::reduce
 4.63%  load_mbo                      (file I/O + decode)
 3.64%  OrderBook::cancel
 3.16%  BookBuilder::apply
 2.07%  main (timing loop)
 1.61%  DbnDecoder::DecodeRecord
 1.44%  unordered_map erase
 1.04%  unordered_map (other)
 1.02%  cfree
```

**Profile read:** The pool eliminated std::list node allocation from the hot path (cfree
dropped 1.58%→1.02%), but `clear_page_erms` barely moved (7.20%→6.97%) — it is now
dominated by `unordered_map` internal allocations (bucket array rehash + per-entry node
allocation), not std::list. The `unordered_map` order-ID index is the largest remaining
engine cost at ~14.2% combined (insert 11.8% + erase 1.4% + other 1.0%).

Next targets: (a) `unordered_map` churn (~14.2%) — `reserve()` to eliminate rehash,
or replace with a flat hash map; (b) intrusive lists (Task 3.7) to improve cache locality
by embedding list pointers directly in the Order struct, eliminating std::list node
indirection.

---

## Task 3.6b — unordered_map reserve (rehash elimination)

One-line change: `index_.reserve(1 << 18)` (256K buckets) in the OrderBook constructor.
Eliminates all rehash cycles during typical sessions.

### AAPL replay (3.95M events, after vs Task 3.6)

| Metric     | Task 3.6 | After  | Delta  |
|------------|----------|--------|--------|
| p50 total  | 51 ns    | 48 ns  | **-6%**  |
| p99 total  | 223 ns   | 218 ns | -2%    |
| p99.9 total| 421 ns   | 476 ns | +13%   |
| Add p50    | 85 ns    | 54 ns  | **-36%** |
| Cancel p50 | 45 ns    | 46 ns  | 0%     |
| Throughput | 8.15M    | 8.84M  | **+8%**  |

### Hardware counters (AAPL, after vs Task 3.6)

| Counter               | Task 3.6       | After          | Delta |
|------------------------|----------------|----------------|-------|
| cycles                 | 2,909,061,729  | 2,737,238,922  | **-6%** |
| instructions           | 5,037,775,923  | 4,977,833,875  | -1%   |
| IPC                    | 1.73           | 1.82           | **+5%** |
| L1-dcache-load-misses  | 1.33%          | 1.45%          | +9%   |
| branch-misses          | 1.42%          | 1.28%          | **-10%** |

### Notes

- Add p50 dropped 36% — rehash was the main contributor to insert latency variance.
- Branch-miss rate dropped 10% — the rehash code path (unpredictable) is never taken.
- L1 miss rate increased slightly (+9%) — the pre-allocated 256K bucket array occupies
  more cache, but the net effect is positive (6% fewer cycles).
- p99.9 regressed slightly (+13%) — occasional cold-cache accesses to the larger bucket
  array; acceptable given the median/throughput improvements.

---

## Task 3.6c — Pool-allocate unordered_map hash nodes

Added a second `FreeListPool` (`index_pool_`) for the order-ID `unordered_map`. Each
insert/erase was calling `malloc`/`free` for hash nodes, with `clear_page_erms` (kernel
page zeroing) at 7.5% in the post-3.6b profile. Pool allocation keeps hash nodes in
compact memory, improving cache locality.

### AAPL replay (3.95M events, after vs Task 3.6b)

| Metric     | Task 3.6b | After  | Delta  |
|------------|-----------|--------|--------|
| p50 total  | 48 ns     | 44 ns  | **-8%**  |
| p99 total  | 218 ns    | 209 ns | -4%    |
| p99.9 total| 476 ns    | 449 ns | **-6%**  |
| Add p50    | 54 ns     | 50 ns  | **-7%**  |
| Cancel p50 | 46 ns     | 43 ns  | **-7%**  |
| Throughput | 8.84M     | 9.21M  | **+4%**  |

### Hardware counters (AAPL, after vs Task 3.6b)

| Counter               | Task 3.6b      | After          | Delta |
|------------------------|----------------|----------------|-------|
| cycles                 | 2,737,238,922  | 2,646,490,770  | **-3%** |
| instructions           | 4,977,833,875  | 4,978,286,221  | 0%    |
| IPC                    | 1.82           | 1.88           | **+3%** |
| L1-dcache-load-misses  | 1.45%          | 1.11%          | **-23%** |
| branch-misses          | 1.28%          | 1.28%          | 0%    |

### Notes

- L1 cache miss rate dropped 23% — pool keeps hash nodes in compact memory instead of
  scattered across malloc'd pages.
- IPC improved to 1.88 — fewer stalls waiting for cache misses.
- Same instruction count as 3.6b (pool doesn't change the code path, just the allocator).
- p99.9 recovered from the 3.6b regression (449 vs 476) thanks to better cache behavior.

---

## Task 3.7 — Intrusive per-level FIFO lists

Embedded `prev`/`next` pointers directly in `Order`; replaced `std::list<Order,
PoolAllocator<Order>>` with a hand-rolled intrusive doubly-linked list (head/tail per
PriceLevel). Locator now holds `Order*` directly. PriceLevel is trivially
default-constructible (no allocator needed).

### Microbenchmarks (after vs Task 3.6c)

| Benchmark          | 3.6c (ns) | After (ns) | Delta |
|--------------------|-----------|------------|-------|
| BM_AddResting      | 48.2      | 42.1       | **-13%** |
| BM_Cancel          | 48.0      | 41.0       | **-15%** |
| BM_MatchSingleLevel | 276      | 268        | -3%   |
| BM_MatchMultiLevel | 1599      | 1978       | +24%  |

### AAPL replay (3.95M events, after vs Task 3.6c)

| Metric     | Task 3.6c | After  | Delta  |
|------------|-----------|--------|--------|
| p50 total  | 44 ns     | 44 ns  | 0%     |
| p99 total  | 209 ns    | 215 ns | +3%    |
| p99.9 total| 449 ns    | 487 ns | +8%    |
| Add p50    | 50 ns     | 52 ns  | +4%    |
| Cancel p50 | 43 ns     | 42 ns  | -2%    |
| Throughput | 9.21M     | 9.16M  | -1%    |

### Hardware counters (AAPL, after vs Task 3.6c)

| Counter               | Task 3.6c      | After          | Delta |
|------------------------|----------------|----------------|-------|
| cycles                 | 2,646,490,770  | 2,672,062,555  | +1%   |
| instructions           | 4,978,286,221  | 4,977,219,752  | 0%    |
| IPC                    | 1.88           | 1.86           | -1%   |
| L1-dcache-load-misses  | 1.11%          | 1.11%          | 0%    |
| branch-misses          | 1.28%          | 1.29%          | 0%    |

### Notes

- **Null result for the replay workload.** The pool was already giving compact allocation;
  removing `std::list` node indirection did not change cache behavior (L1 miss 1.11%→1.11%).
- Microbench single-op latency improved (AddResting -13%, Cancel -15%) — simpler
  push_back/erase vs std::list iterator operations — but these gains are dwarfed by
  unordered_map cost in the full replay.
- MatchMultiLevel regressed +24% — the larger Order struct (added prev/next = +16 bytes)
  reduces cache density during multi-level walks. Acceptable: real replay is flat.
- Kept as an architecture/clarity win: code is simpler, no std::list dependency, Locator
  holds Order* directly, and microbench single-op latencies are at their project best
  (42ns add, 41ns cancel from a 62ns/50ns baseline).

---

## Task 3.8 — Pack Order to one cache line + Task 3.9 — Branch hints

**Task 3.8:** Shrunk Order from 72 to 64 bytes by making Side/OrderKind/TimeInForce
enums `uint8_t` (was `int`). Reordered fields: hot (quantity, next, prev, order_id,
price) first, cold (original_quantity, sequence) last. `static_assert(sizeof(Order)==64)`.

**Task 3.9:** Added `[[likely]]`/`[[unlikely]]` on hot-path branches (ensure in-band,
overflow, zero-qty, FOK, cancel-not-found). Confirmed no virtual dispatch in the
matching engine (virtuals only in ITCH parser handler).

### AAPL replay (3.95M events, after vs Task 3.7)

| Metric     | Task 3.7 | After  | Delta  |
|------------|----------|--------|--------|
| p50 total  | 44 ns    | 45 ns  | +2%    |
| p99 total  | 215 ns   | 214 ns | 0%     |
| p99.9 total| 487 ns   | 464 ns | -5%    |
| Throughput | 9.16M    | 9.13M  | 0%     |

### Hardware counters (AAPL, after vs Task 3.7)

| Counter               | Task 3.7       | After          | Delta |
|------------------------|----------------|----------------|-------|
| cycles                 | 2,672,062,555  | 2,655,728,100  | -1%   |
| instructions           | 4,977,219,752  | 4,974,754,553  | 0%    |
| IPC                    | 1.86           | 1.87           | +1%   |
| L1-dcache-load-misses  | 1.11%          | 1.09%          | **-2%** |
| branch-misses          | 1.29%          | 1.29%          | 0%    |

### Microbenchmarks

| Benchmark          | 3.7 (ns) | After (ns) | Delta |
|--------------------|----------|------------|-------|
| BM_AddResting      | 42.1     | 41.2       | -2%   |
| BM_Cancel          | 41.0     | 41.0       | 0%    |
| BM_MatchSingleLevel | 268     | 268        | 0%    |
| BM_MatchMultiLevel | 1978     | 1834       | **-7%** |

### Notes

- **Marginal result.** L1 miss improved slightly (1.11%→1.09%) from 64→72 byte struct
  packing. Branch-miss rate unchanged — the predictor was already at 1.29%.
- MatchMultiLevel recovered -7% — the smaller Order (64 vs 72 bytes) improves cache
  density during multi-level walks, partially offsetting the 3.7 regression.
- `[[likely]]`/`[[unlikely]]` had no measurable effect — GCC already predicted the
  common paths correctly. Kept for documentation value.
- Two consecutive optimizations below ~5% — hitting the stopping criterion per the plan.

---

## Running numbers log

| Task | Change | p50 (ns) | p99 (ns) | p99.9 (ns) | Throughput (msg/s) | L1 miss % | IPC | Branch miss % | Notes |
|------|--------|----------|----------|------------|--------------------|-----------|----|---------------|-------|
| 3.4  | Baseline (AAPL) | 103 | 329 | 1163 | 5.74M | 1.53% | 1.64 | 1.77% | std::map ~15%, unordered_map ~17% of engine |
| 3.5  | Flat tick-indexed array | 56 | 269 | 698 | 7.47M | 1.42% | 1.77 | 1.33% | p50 -46%, throughput +30%, O(1) level lookup |
| 3.6  | FreeListPool allocator | 51 | 223 | 421 | 8.15M | 1.33% | 1.73 | 1.42% | p99.9 -40%, throughput +9%, no per-node malloc |
| 3.6b | unordered_map reserve | 48 | 218 | 476 | 8.84M | 1.45% | 1.82 | 1.28% | Add p50 -36%, throughput +8%, no rehash |
| 3.6c | Pool hash nodes | 44 | 209 | 449 | 9.21M | 1.11% | 1.88 | 1.28% | L1 miss -23%, IPC +3%, compact hash nodes |
| 3.7  | Intrusive lists | 44 | 215 | 487 | 9.16M | 1.11% | 1.86 | 1.29% | Null for replay; microbench add -13%, cancel -15% |
| 3.8+3.9 | Struct pack + branch hints | 45 | 214 | 464 | 9.13M | 1.09% | 1.87 | 1.29% | Marginal; L1 miss -2%, MatchMulti -7% |
