# Limit Order Book & Matching Engine

A single-threaded, price-time-priority limit order book and matching engine in C++20,
validated against real Nasdaq market data.

## How it works

The engine maintains two sides of a limit order book:

- **Bids** sorted highest-first (best bid = highest price).
- **Asks** sorted lowest-first (best ask = lowest price).

Within each price level, orders are queued in FIFO (time-priority) order. An incoming
aggressive order matches against the best resting orders on the opposite side until it is
filled, can no longer cross the spread, or the opposite side is empty. Every fill produces
a `Trade` record with the matched price and quantity.

**All prices are signed 64-bit integers — never floating point.** Fixed-point scales differ
by data source (Databento: 1e-9 nanodollars; ITCH: 1e-4) and are never mixed.

## Supported order types

| Type | Behavior |
|------|----------|
| **Limit** | Match what crosses; rest the remainder at the limit price. |
| **Market** | Match against best available; never rests; unfilled qty discarded. |
| **IOC** | Match what crosses immediately; discard the remainder. |
| **FOK** | Fill entirely or reject — pre-scans liquidity before executing. |

Cancel, reduce, and modify (cancel + re-add on price change) are also supported.

## Performance

Replaying 3.95M AAPL order events on a GCP `c4-standard-4` (Intel Granite Rapids):

| Metric | Baseline | Optimized | Improvement |
|--------|----------|-----------|-------------|
| p50 latency | 103 ns | 45 ns | **-56%** |
| p99 latency | 329 ns | 214 ns | **-35%** |
| Throughput | 5.74M msg/s | 9.13M msg/s | **+59%** |

Key changes: flat tick-indexed array replacing `std::map` (O(1) level lookup), pool
allocation eliminating per-order `malloc`/`free`, and intrusive FIFO lists. Full
methodology, per-optimization deltas, and hardware-counter analysis in
[`docs/phase3-writeup.md`](docs/phase3-writeup.md).

## Phases

### Phase 1 — Correct matching engine
Core types, book structure, matching logic for all order types, cancel/modify,
edge-case hardening, and randomized property/invariant tests.

### Phase 2A — Databento MBO → MBP-10 validation
Drive the engine from Databento's historical MBO (L3, market-by-order) feed and diff
the reconstructed book against the MBP-10 (top-10 levels) reference, event-for-event,
for full trading sessions (INTC + AAPL on XNAS.ITCH).

### Phase 2B — Raw NASDAQ ITCH 5.0 parser
Parse the big-endian, length-prefixed ITCH 5.0 binary format directly, replay through
the engine, and validate structural invariants over a full session.

### Phase 3 — Performance optimization
Profile-guided optimization with before/after measurements on every change.
See [`docs/phase3-perf.md`](docs/phase3-perf.md) for the numbers log and
[`docs/phase3-writeup.md`](docs/phase3-writeup.md) for the narrative.

## Build

```bash
cmake -B build
cmake --build build
ctest --test-dir build
```

Requires CMake >= 3.20 and a C++20 compiler (GCC 11+, Clang 14+, Apple Clang 14+).

## Data

Market data files are gitignored (large / licensed). See [`data/README.md`](data/README.md)
for how to obtain them. All tests that depend on external data skip gracefully when the
files are absent — a fresh clone builds and tests green with no data.

## References

- [NASDAQ TotalView-ITCH 5.0 Specification](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf)
- [Databento Historical API](https://databento.com/docs/api-reference-historical)
- [Databento MBO Schema](https://databento.com/docs/schemas-and-data-formats/mbo)
