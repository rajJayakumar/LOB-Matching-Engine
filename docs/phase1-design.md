# Phase 1 — Engine Design

## Data structures

### Two ordered maps for the book sides

- **Bids:** `std::map<Price, PriceLevel, std::greater<>>` — highest price first, so
  `begin()` is the best bid. Price-time priority is automatic: the map orders by price,
  and within each level, a FIFO queue orders by time.
- **Asks:** `std::map<Price, PriceLevel>` (default `std::less`) — lowest price first, so
  `begin()` is the best ask.

Using `std::map` gives O(log N) insert/lookup by price, which is acceptable for Phase 1's
correctness-first goal. Phase 3 would replace this with a flat array indexed by price.

### FIFO lists within each level

`PriceLevel` contains a `std::list<Order>`. `std::list` provides:
- Stable iterators (not invalidated by other inserts/erases), which the cancel index relies on.
- O(1) erase-by-iterator for cancel.
- FIFO ordering: new orders are `push_back`'d; matching iterates from `begin()`.

### Cancel index

`std::unordered_map<OrderId, Locator>` maps every resting order to a `Locator` holding the
side, price, and `std::list<Order>::iterator`. This gives O(1) cancel/reduce lookup without
scanning the book. The index is kept consistent on every add, fill, cancel, reduce, and modify.

## Integer-price invariant

All prices are `int64_t`. No floating-point arithmetic touches prices anywhere in the engine.
This is non-negotiable because:
- Float equality is unreliable (`0.1 + 0.2 != 0.3`), and the book depends on exact price matching.
- Price-time priority requires deterministic ordering — float rounding would silently corrupt it.
- Data sources (Databento 1e-9, ITCH 1e-4) deliver prices as integers in different fixed-point
  scales. Storing the raw integer avoids all conversion error.

## Matching algorithm

1. `add(order)` assigns a monotonic sequence number, then matches against the opposite side.
2. `match_against()` walks the opposite book in price-time order. For each resting order at
   the best available price, fill `min(aggressor_remaining, resting_remaining)` and emit a
   `Trade`. Remove fully filled resting orders and empty levels.
3. Price-crossing check: a limit buy crosses if `buy_price >= ask_price`; a limit sell crosses
   if `sell_price <= bid_price`. Market orders skip the check entirely (fill until empty).
4. After matching, GTC limit remainder rests; IOC/Market remainder is discarded; FOK
   pre-scans before any matching and rejects if the full quantity is not available.

## Order types

| Type | Pre-scan | Matches | Remainder |
|------|----------|---------|-----------|
| Limit GTC | No | Yes | Rests |
| Limit IOC | No | Yes | Discarded |
| Limit FOK | Yes — must fill entirely | Yes (if passes) | N/A |
| Market | No | Yes (all levels) | Discarded |

## Modify semantics

- **Same price:** quantity updated in-place; time priority preserved.
- **Price change:** modeled as cancel + re-add. The order gets a new sequence number and
  goes to the back of the queue at the new price level. This matches MBO `M` and ITCH `U`
  semantics where a price-changing modify loses time priority.

## Testing strategy

1. **Scenario tests** (`test_matching`, `test_order_types`, `test_cancel`): hand-crafted
   sequences with expected trade outputs. Cover single crossing, multi-level walk, market
   orders, IOC, FOK, cancel, reduce, modify.
2. **Edge cases** (`test_edge_cases`): exact level exhaustion, zero-qty rejection,
   interleaved add/cancel/match, never-crossed book.
3. **Randomized property tests** (`test_invariants`): 50,000 operations across 5 fixed
   seeds. After every operation, assert:
   - Book is never crossed (best bid < best ask).
   - No empty price levels linger.
   - Cancel index count matches total resting orders.
   - Share conservation: aggressor filled == sum of resting reductions.
