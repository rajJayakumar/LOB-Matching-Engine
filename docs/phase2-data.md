# Phase 2 — Real-Data Validation

Phase 2 validates the matching engine against real market data in two independent tracks:

- **Phase 2A (primary):** drive the engine from Databento MBO (L3) and diff the reconstructed
  book against Databento MBP-10 (L2) — same instrument, same session, event-for-event.
- **Phase 2B (secondary):** parse raw NASDAQ TotalView-ITCH 5.0 binary data and replay it
  through the engine, validated by structural sanity checks and invariant coherence.

Keeping the tracks separate means a book-diff failure is unambiguously a matching-logic bug,
and a parser failure is unambiguously a decode bug.

---

## Phase 2A — Databento MBO vs MBP-10

### Why this gives a clean same-day diff

Databento's `XNAS.ITCH` dataset provides two views of the same exchange feed:

- **MBO** (market-by-order, L3): one record per order action — `A` add, `C` cancel/reduce,
  `M` modify, `R` clear, `T` trade, `F` fill. Each record carries `order_id`, `price`, `size`,
  `action`, `side`, `sequence`, `flags`.
- **MBP-10** (market-by-price, top-10 L2): one record per book-changing event with the top-10
  price levels (`bid_px_00..09`, `ask_px_00..09`, `bid_sz_00..09`, etc.).

Both come from the same underlying ITCH feed, so when correctly aligned they agree exactly.
The engine processes MBO to build a book; MBP-10 serves as the authoritative reference.

### Gotchas discovered during development

**Price scale: 1e-9 (nanodollars).** Databento prices are fixed-point integers in units of
10^-9, e.g. $585.32 = `585320000000`. `UNDEF_PRICE` (`INT64_MAX`) is the null sentinel and
must be guarded explicitly — it appears on `R` (clear) records and some `T` records.

**Event-boundary alignment.** MBP-10 emits one record per book-changing event. MBO emits one
record per order action, and a single event can span multiple MBO records. Apply MBO records
in `sequence` order and compare to MBP-10 only at the event boundary — identified by the
`FLAG_LAST` bit in the `flags` field. Comparing mid-event produces phantom divergences.

**T (trade) and F (fill) are informational for Nasdaq.** Neither `T` nor `F` mutates the book.
The paired `C` (cancel) record removes liquidity from the resting side. `F`'s price field is
the trade price, not the resting order's price — applying `F` as a book mutation removes the
wrong level. `T` records with `FLAG_LAST` do not produce a corresponding MBP-10 record, so
the alignment logic must skip them.

**Session-open start.** Historical MBO has no prepended snapshot. Starting mid-session means
the book is missing resting orders, causing spurious divergences. Always pull from session
open (13:30 UTC in EDT / 14:30 UTC in EST for 09:30 ET).

**Price-changing modify loses time priority.** An MBO `M` action that changes the price is
modeled as cancel + re-add with a new sequence number.

### Results

- **INTC** (2025-06-04, full session): 301,203+ events matched to 10 levels, zero divergences.
- **AAPL** (2025-06-04, full session): 100,000+ events matched to 10 levels, zero divergences.

Both tests skip gracefully when the DBN files are absent.

---

## Phase 2B — Raw NASDAQ ITCH 5.0

### Parser design

The parser handles the raw BinaryFILE format from NASDAQ TotalView-ITCH 5.0:

- **Big-endian framing.** The file is a stream of messages, each preceded by a 2-byte
  big-endian length prefix. Every multi-byte integer field (prices, quantities, timestamps,
  order references) is big-endian and must be byte-swapped on little-endian x86.
- **Price scale: 1e-4.** Prices are 4-byte unsigned integers in units of 10^-4. This is
  a different scale from Databento's 1e-9 — the two are never mixed or compared.
- **6-byte timestamps.** Nanoseconds since midnight, decoded from 6 big-endian bytes.
- **Order references by ID only.** `E` (executed), `C` (executed with price), `X` (cancel),
  `D` (delete), and `U` (replace) reference orders by their 8-byte order reference number
  with no price field. The replay handler looks up the price via an order-ID index —
  exercising the same O(1) cancel index from Phase 1.

Message types handled: `S` (system event), `R` (stock directory), `A` (add order),
`F` (add order with MPID), `E` (order executed), `C` (order executed with price),
`X` (order cancel), `D` (order delete), `U` (order replace).

### Replay mapping

| ITCH type | Engine action |
|-----------|---------------|
| A / F | `add_resting` (limit order at the ITCH price) |
| E / C | `reduce` by executed shares |
| X | `reduce` by cancelled shares |
| D | `cancel` (full removal) |
| U | `cancel` old order + `add_resting` new order (new price/size, loses time priority) |

The replay filters to a single target symbol using the `R` (stock directory) message to
learn the stock locate code, then processes only messages matching that locate.

### Results

**Structural validation** (parser test on the full 9.5 GB sample, Aug 30 2019):
- 310M+ messages parsed with zero buffer overruns
- All message types present with non-zero counts
- Zero order-reference misses (every E/C/X/D/U resolves against a prior add)

**Invariant coherence** (MSFT full-session replay):
- 1.5M+ events applied through the engine
- Book invariants checked every 100 events (~15,000+ checks)
- Zero violations: book never crossed, no lingering empty levels, cancel index consistent

Both tests skip gracefully when the ITCH sample file is absent.

---

## Data sources

See `data/README.md` for exact instructions on obtaining the data files.
Both Databento DBN files and the ITCH sample are large and gitignored — they are never
committed to the repository. All data-dependent tests skip cleanly on a fresh clone.
