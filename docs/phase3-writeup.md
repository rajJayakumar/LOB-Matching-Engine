# Optimization Journey: Making a Limit Order Book Fast

A profile-guided optimization of a C++ limit order book and matching engine,
validated on real Nasdaq market data. Every change was measured before and after;
correctness was verified at every step against Databento MBO→MBP-10 diffs and
ITCH full-session replay invariants.

---

## The headline

Replaying 3.95 million AAPL order events (a full Nasdaq session):

| Metric | Baseline | Optimized | Improvement |
|--------|----------|-----------|-------------|
| p50 latency | 103 ns | 45 ns | **-56%** |
| p99 latency | 329 ns | 214 ns | **-35%** |
| p99.9 latency | 1163 ns | 464 ns | **-60%** |
| Throughput | 5.74M msg/s | 9.13M msg/s | **+59%** |
| IPC | 1.64 | 1.87 | +14% |
| L1 d-cache miss rate | 1.53% | 1.09% | -29% |
| Branch mispredict rate | 1.77% | 1.29% | -27% |

---

## Measurement environment

All numbers come from a GCP `c4-standard-4` (Intel Granite Rapids, 4 vCPUs) Linux VM
with the PMU enabled at `standard` level — the same class of machine a quant desk uses
for benchmarking. Never the macOS dev laptop (which cannot run `perf`).

- **Compiler:** GCC 14.2.0, `-O3 -fno-omit-frame-pointer -march=native`
- **Profiling:** `perf record` / `perf stat` with the explicit event list
  `cycles,instructions,branches,branch-misses,L1-dcache-loads,L1-dcache-load-misses`
  (the `standard` PMU does not expose LLC events; this workload is an L1/L2 story)
- **Timer:** `clock_gettime(CLOCK_MONOTONIC_RAW)` with measured overhead (25 ns median
  on Granite Rapids) subtracted from every sample; timed operations guarded against
  dead-code elimination with `DoNotOptimize` / `ClobberMemory`
- **Workload:** Databento historical MBO for AAPL on XNAS.ITCH — 3.95M events from
  session open (book builds from empty, no snapshot)

---

## Starting point: why was it slow?

The Phase 1/2 engine was built for correctness, not speed. The data structures were
textbook STL:

- **Price levels:** `std::map<Price, PriceLevel>` — a red-black tree. Every level
  lookup is O(log N) with pointer chasing through heap-allocated nodes. Each node
  is a separate allocation, scattered across memory.
- **Per-level FIFO:** `std::list<Order>` — another doubly-linked list of
  heap-allocated nodes. Every add/cancel allocates or frees a list node.
- **Order-ID index:** `std::unordered_map<OrderId, Locator>` — hash table with
  per-entry node allocation and occasional full-table rehash.

### What the profiler showed

`perf record` on the AAPL replay:

```
41.3%  BookBuilder::apply (engine hot path)
  22.6%  OrderBook::rest
    14.8%  unordered_map::operator[] (index insert)
  14.7%  OrderBook::reduce / cancel
    9.5%   OrderBook::cancel
13.4%  load_mbo (file I/O — outside the engine)
17.3%  clock_gettime (timer overhead — subtracted)
```

The `std::map` red-black tree accounted for ~15% of engine time (level lookup +
per-node allocation on both sides). The `unordered_map` order-ID index accounted for
~17% (insert, erase, and rehash). Per-order `malloc`/`free` from `std::list` and
`unordered_map` node allocation was a further ~9%.

The profile dictated the optimization order: eliminate the tree first (biggest
structural cost), then the per-order allocation, then the list overhead.

---

## Optimization 1: Flat tick-indexed price-level array

**What the profile said:** `std::map` level lookup + node allocation = ~15% of engine time.

**The problem:** A red-black tree stores price levels as heap-allocated nodes linked by
pointers. Looking up a price level is O(log N) — but worse than the asymptotic cost, every
tree traversal chases 3-5 pointers to nodes scattered across memory. On a modern CPU, each
pointer chase is a potential L1 cache miss (~4 ns penalty on Granite Rapids). For a book
with 50-100 live levels per side, that is 6-7 pointer dereferences per lookup, most of them
cache misses.

**The fix:** Replace both `std::map<Price, PriceLevel>` sides with a flat
`std::vector<PriceLevel>` indexed by tick offset: `index = (price - base) / tick_size`.
Level lookup becomes a single array access — O(1), no pointer chasing, cache-line-friendly.
Best-bid/best-ask are maintained as cursors that advance to the next non-empty level on
exhaustion (no rescan from the end).

Out-of-band prices (rare stub quotes far from the inside market) spill to a small overflow
`std::map`; in practice < 0.1% of orders hit it. The band is initialized once on the first
order per side; a single-day session stays well within the range.

**Result:**

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| p50 total | 103 ns | 56 ns | **-46%** |
| p99 total | 329 ns | 269 ns | -18% |
| Throughput | 5.74M | 7.47M | **+30%** |
| L1 d-cache miss | 1.53% | 1.42% | -7% |
| IPC | 1.64 | 1.77 | +8% |
| Branch miss | 1.77% | 1.33% | -25% |

This was the largest single improvement — the interview centerpiece. The `std::map`
overhead (~15% of the profile) disappeared entirely from the hot path.

---

## Optimization 2: Pool allocation for order nodes

**What the profile said (post-flat-array):** `clear_page_erms` (kernel page zeroing for
`malloc`'d `std::list` nodes) = 7.2%, `cfree` = 1.6%. Per-node allocation/deallocation was
the new #2 cost after the order-ID index.

**The problem:** Every `std::list::push_back` calls `malloc`; every erase calls `free`.
The kernel zeroes fresh pages (`clear_page_erms`), and the allocator's free-list
management adds overhead. Worse, allocated nodes are scattered across the heap, degrading
cache locality.

**The fix:** A `FreeListPool` that pre-allocates Order-sized slots in 8192-slot blocks.
Acquire and release are O(1) — pop/push on an intrusive free list. Wrapped in an
STL-compatible `PoolAllocator<T>` so `std::list` uses it transparently. No per-order
`malloc`/`free` in steady state; the pool grows in blocks if exhausted.

**Result:**

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| p50 total | 56 ns | 51 ns | -9% |
| p99 total | 269 ns | 223 ns | -17% |
| p99.9 total | 698 ns | 421 ns | **-40%** |
| Throughput | 7.47M | 8.15M | +9% |

The p99.9 improvement came from eliminating the heavy tail caused by kernel page zeroing.

---

## Optimization 3: Reserve unordered_map buckets

**What the profile said:** `unordered_map::operator[]` = 11.8% — dominated by occasional
full-table rehash when the load factor exceeded the threshold.

**The fix:** One line: `index_.reserve(1 << 18)` (256K buckets) in the OrderBook
constructor. Pre-sizes the hash table so no rehash occurs during a typical session.

**Result:**

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| Add p50 | 85 ns | 54 ns | **-36%** |
| Throughput | 8.15M | 8.84M | +8% |
| Branch miss | 1.42% | 1.28% | -10% |

The rehash code path is unpredictable (taken rarely, but when taken it touches every
bucket); eliminating it dropped the branch mispredict rate by 10%.

---

## Optimization 4: Pool-allocate hash-map nodes

**What the profile said:** `clear_page_erms` = 7.0% — still present, now from
`unordered_map` internal node allocation (not `std::list`, which was already pooled).

**The fix:** A second `FreeListPool` backing the `unordered_map`'s allocator. Hash nodes
now come from compact, pre-allocated memory instead of scattered `malloc` pages.

**Result:**

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| p50 total | 48 ns | 44 ns | -8% |
| L1 d-cache miss | 1.45% | 1.11% | **-23%** |
| IPC | 1.82 | 1.88 | +3% |

The L1 miss rate dropped 23% — pool-allocated hash nodes are packed together, so
successive lookups hit warm cache lines instead of faulting across the heap.

---

## Optimization 5: Intrusive per-level FIFO lists

**What the profile said:** `std::list` iterator operations and node indirection were the
remaining per-level cost, though dwarfed by the hash-map index.

**The fix:** Embed `prev`/`next` pointers directly in `Order`; replace `std::list` with a
hand-rolled intrusive doubly-linked list (head/tail per `PriceLevel`). The cancel locator
holds `Order*` directly — no iterator indirection. `PriceLevel` becomes trivially
default-constructible (no allocator state needed).

**Result:** Null for the full replay workload — the pool was already providing compact
allocation. Microbenchmark isolated operations improved (AddResting -13%, Cancel -15%),
but these gains were masked by the hash-map cost in the replay. Kept as an
architecture/clarity win: simpler code, no `std::list` dependency, direct pointer
cancel.

---

## Optimization 6: Struct packing and branch hints

**The fix:** Shrunk `Order` from 72 to 64 bytes (one cache line) by making enum fields
`uint8_t`. Added `[[likely]]`/`[[unlikely]]` on hot-path branches.

**Result:** Marginal. L1 miss rate improved 1.11%→1.09%. Branch-miss rate unchanged
(GCC's predictor was already correct). `MatchMultiLevel` microbenchmark recovered -7%
from better cache density.

This was the second consecutive optimization below ~5% p99 improvement, hitting the
plan's stopping criterion.

---

## What stopped mattering

After six rounds of optimization, the profile looked like this:

```
20.6%  clock_gettime  (timer overhead — not engine)
11.8%  unordered_map  (order-ID index — fundamental to the design)
 4.6%  load_mbo       (file I/O — outside the engine)
 4.8%  OrderBook::rest
 4.7%  OrderBook::reduce
 3.6%  OrderBook::cancel
```

The remaining engine cost (~13%) is spread across actual book logic — there is no single
data-structure bottleneck left to attack. The `unordered_map` order-ID index (~12%) is
inherent to O(1) cancel-by-ID; replacing it with a flat hash map would be the next step,
but the profile shows diminishing returns at this point.

Timer overhead (20.6%) inflates wall-clock time but does not affect the engine's actual
per-operation latency (it is measured and subtracted). File I/O (4.6%) is outside the
engine entirely.

---

## Per-optimization summary

| # | Change | p50 | p99 | p99.9 | Throughput | Key counter |
|---|--------|-----|-----|-------|------------|-------------|
| 1 | Flat tick-indexed array | 103→56 ns | 329→269 ns | 1163→698 ns | 5.74→7.47M | L1 miss 1.53→1.42% |
| 2 | Pool allocator | 56→51 ns | 269→223 ns | 698→421 ns | 7.47→8.15M | p99.9 -40% |
| 3 | Reserve hash buckets | 51→48 ns | 223→218 ns | 421→476 ns | 8.15→8.84M | Branch miss 1.42→1.28% |
| 4 | Pool hash nodes | 48→44 ns | 218→209 ns | 476→449 ns | 8.84→9.21M | L1 miss 1.45→1.11% |
| 5 | Intrusive lists | 44→44 ns | 209→215 ns | 449→487 ns | 9.21→9.16M | Null (arch win) |
| 6 | Struct pack + hints | 44→45 ns | 215→214 ns | 487→464 ns | 9.16→9.13M | Marginal |

---

## Scaling

The matching core is single-threaded by design — no locks, no atomics, no contention.
This is how production matching engines work: **one thread per symbol, one order book
per thread.** Scaling is horizontal: shard by symbol across cores. Each book processes
its stream in isolation, and the kernel schedules threads across cores. The
single-threaded design is not a limitation — it is the architecture that eliminates
the synchronization cost that would otherwise dominate at these latencies.

---

## Methodology notes

- **Every optimization was measured before and after.** The baseline was captured before
  any engine code was modified. Each change was a single commit with the delta recorded
  in the commit body and in [`docs/phase3-perf.md`](phase3-perf.md).
- **Correctness was verified at every step.** The full test suite (47 tests: unit,
  edge-case, randomized invariant/fuzz), the INTC and AAPL MBO→MBP-10 10-level diffs,
  and the ITCH full-session replay invariants all passed after every optimization.
  Correctness was never traded for speed.
- **Timer overhead matters at these latencies.** At sub-100 ns per operation,
  `clock_gettime` overhead (25 ns median) is significant. It was measured once and
  subtracted from all samples. Timed operations were guarded against dead-code
  elimination with `benchmark::DoNotOptimize` / `benchmark::ClobberMemory`.
- **Release builds only.** All numbers are from `-O3` builds with frame pointers kept
  for `perf` stack walking. No debug-build numbers were ever quoted.
- **One change per commit.** Each optimization changed exactly one thing so the delta
  was attributable. Negative results (intrusive lists had no replay impact; branch hints
  had no effect) were logged honestly rather than hidden.

---

*Full per-task details, hardware counter tables, and profile snapshots:
[`docs/phase3-perf.md`](phase3-perf.md).*
