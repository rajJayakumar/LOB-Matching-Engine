# Limit Order Book + Matching Engine — Build Plan (Phases 1 & 2)

**Handoff spec for Claude Code.** This document defines the work for a C++ limit order book and
matching engine, from an empty repo through a correct engine (Phase 1) validated on real market
data (Phase 2). Execute the tasks in order. Each task ends with a single git commit using the exact
message given, so the finished repo reads as a clear engineering journey.

Performance optimization (Phase 3) is explicitly **out of scope** for this plan — build for
correctness and clarity, not speed. Do not pre-optimize.

---

## 1. Context and goal

Build an in-memory, single-threaded matching engine that maintains a price-time-priority limit
order book and supports the common order types. Then validate it on real market data in two tracks:

- **Primary (Phase 2A):** prove the **book-building logic** matches a trusted reference by driving
  the engine from Databento's **MBO** (market-by-order, L3) feed and diffing the reconstructed book
  against Databento's **MBP-10** (market-by-price, top-10) feed — same instrument, same session,
  event-for-event. Both come from one source (`XNAS.ITCH` historical), so they align exactly.
- **Secondary (Phase 2B):** demonstrate raw binary-protocol handling by parsing a **raw NASDAQ
  TotalView-ITCH 5.0** sample file (big-endian, length-prefixed framing) and running it through the
  engine, validated structurally and by invariant-coherence.

Correctness is the whole objective: a fast engine that produces wrong fills is worthless. Every
feature is gated by tests. The headline claim for the repo: *"I reconstruct the Nasdaq book from
Databento MBO, validated against MBP-10 to 10 levels across a full session, and I parse the raw
ITCH 5.0 wire format directly."*

**Target reader of the final repo:** a quant-developer interviewer. The commit history, the tests,
and the README are as much a deliverable as the code.

---

## 2. Ground rules (read before writing any code)

- **Language:** C++20. Use RAII throughout; no raw `new`/`delete` in application code.
- **Prices are integers. Never floating point.** Represent every price as a signed 64-bit integer.
  **The fixed-point scale differs by source:** Databento MBO/MBP-10 prices are in units of `1e-9`
  (nanodollars — e.g. $585.32 = `585320000000`); NASDAQ ITCH prices are in units of `1e-4`. Store
  the raw integer in its native scale and **never mix scales within a comparison**. Databento uses
  `INT64_MAX` (`UNDEF_PRICE`) as a null-price sentinel — guard for it explicitly. Floats silently
  corrupt equality/ordering and break price-time priority. Non-negotiable.
- **Quantities** are unsigned 64-bit (or signed if that simplifies guards — be consistent).
- **Single-threaded.** Do not add threads or locks to the matching core. Real matching engines are
  single-threaded per symbol; concurrency here is the wrong instinct and out of scope.
- **Historical data only.** The Databento pull uses the **historical** client
  (`timeseries.get_range`) exclusively. Never use the live client — it requires a Nasdaq
  professional license the project does not need and must not depend on.
- **Secrets:** the Databento API key lives in an environment variable `DATABENTO_API_KEY`, loaded
  from a **gitignored** `.env` (commit a `.env.example` documenting the variable). Never hardcode or
  commit the key.
- **Build system:** CMake (>= 3.20). **Test framework:** GoogleTest via CTest. **DBN decode:**
  `databento-cpp` (via FetchContent; its DBN decoder needs `zstd`). **Data pull:** Python `databento`
  package (used only by `scripts/pull_databento.py`, not by the C++ build).
- **Every commit must build green and pass the full test suite.** Run `ctest` before each commit.
  Never commit a red build. Tests that depend on large external data files must **skip gracefully**
  (not fail) when the data is absent, so a fresh clone without data still goes green.
- **Conventional Commits** format (`type(scope): summary`). Use the exact messages specified.
- **One commit per task**, in the order listed, unless a task explicitly says otherwise.
- Keep functions small and readable; favor clarity over cleverness in these phases.

---

## 3. Repository layout

Create and maintain this structure:

```
order-book/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── .env.example                 # documents DATABENTO_API_KEY (real .env is gitignored)
├── docs/
│   ├── phase1-design.md
│   └── phase2-data.md
├── scripts/
│   └── pull_databento.py        # historical API pull (MBO + MBP-10 → DBN)
├── include/ob/
│   ├── order.hpp
│   ├── order_book.hpp
│   ├── trade.hpp
│   ├── dbn/                     # Phase 2A
│   │   └── loader.hpp
│   └── itch/                    # Phase 2B
│       ├── messages.hpp
│       ├── parser.hpp
│       └── replay.hpp
├── src/
│   ├── order_book.cpp
│   ├── dbn/
│   │   └── loader.cpp
│   └── itch/
│       ├── parser.cpp
│       └── replay.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── test_matching.cpp
│   ├── test_order_types.cpp
│   ├── test_cancel.cpp
│   ├── test_edge_cases.cpp
│   ├── test_invariants.cpp      # property/fuzz tests (expose a reusable invariant checker)
│   ├── dbn/                     # Phase 2A
│   │   └── test_book_diff.cpp
│   └── itch/                    # Phase 2B
│       ├── test_parser.cpp
│       └── test_replay_invariants.cpp
├── apps/
│   ├── dbn_diff_main.cpp        # Phase 2A CLI: MBO→book diffed vs MBP-10
│   └── replay_main.cpp          # Phase 2B CLI: raw ITCH replay
└── data/
    ├── README.md                # how to obtain the datasets (files gitignored)
    ├── databento/               # pulled DBN files (gitignored)
    └── itch/                    # NASDAQ ITCH sample, gunzipped (gitignored)
```

`.gitignore` must exclude build directories, `.env`, and all market data (`data/databento/*`,
`data/itch/*`, `*.dbn`, `*.gz`, `*.NASDAQ_ITCH50`). Market data is large and/or licensed — **never
commit it**. Small synthetic fixtures used by unit tests live under `tests/` and are fine to commit.

---

## 4. Commit discipline (important — this is a graded output)

The git history is a recruiter-facing artifact. It must tell the story of the build: setup →
correct engine, feature by feature → engine validated against a trusted reference → raw-feed parser.
Rules:

1. Commit after **every** task below, using the exact message provided.
2. Each commit is a **green, self-contained increment** — it builds and all tests pass.
3. Use Conventional Commits scopes consistently: `core`, `book`, `match`, `orders`, `dbn`, `itch`,
   `replay`, plus `chore`, `docs`, `test`.
4. Write real commit bodies where useful (a sentence or two on what and why), not just the summary
   line. An interviewer reading `git log` should understand the progression without the code. Where
   a task involved debugging a real-data divergence, say so in the body — that is exactly the
   problem-solving story worth showing.
5. Do **not** squash. The granularity is the point.

---

## 5. Phase 0 — Project setup

### Task 0.1 — Scaffold the CMake project
- **Create:** `CMakeLists.txt`, `tests/CMakeLists.txt`, minimal `include/ob/order.hpp` placeholder,
  `.gitignore`, `.env.example`, and one trivial passing test so `ctest` runs green.
- **Requirements:** C++20 standard set; GoogleTest fetched via `FetchContent`; CTest enabled;
  a `hello`-level test asserting `true`. `.env.example` documents `DATABENTO_API_KEY`.
- **Done when:** `cmake -B build && cmake --build build && ctest --test-dir build` all succeed.
- **Commit:** `chore: scaffold CMake project with GoogleTest and CTest`

### Task 0.2 — Project README and roadmap
- **Create:** `README.md` describing the project, the price-time-priority model in a few sentences,
  the phase roadmap, and build/run instructions. Add `data/README.md` placeholder.
- **Done when:** README builds a clear picture of intent; links to the ITCH spec and Databento docs.
- **Commit:** `docs: add README with project goals and roadmap`

---

## 6. Phase 1 — A correct matching engine

### Task 1.1 — Core types
- **Create:** `include/ob/order.hpp`, `include/ob/trade.hpp`.
- **Requirements:**
  - `enum class Side { Buy, Sell };`
  - `enum class TimeInForce { GTC, IOC, FOK };` and an order-kind distinction for Market vs Limit.
  - `Order`: `order_id` (u64), `side`, `price` (i64 ticks; ignored/`0` for market), `quantity`
    (remaining), original quantity, a monotonic `sequence` number for time priority.
  - `Trade`: aggressor id, resting id, `price`, `quantity`, sequence/timestamp.
  - No floats anywhere. Add a comment stating the integer-price invariant.
- **Done when:** header compiles; a test constructs and reads back an `Order` and a `Trade`.
- **Commit:** `feat(core): add Order and Trade types with integer tick prices`

### Task 1.2 — OrderBook skeleton and interface
- **Create:** `include/ob/order_book.hpp`, `src/order_book.cpp`.
- **Requirements:** define the public interface and internal structures, no matching logic yet:
  - Bids: `std::map<Price, PriceLevel, std::greater<>>` (best = highest = `begin()`).
  - Asks: `std::map<Price, PriceLevel>` (best = lowest = `begin()`).
  - `PriceLevel`: FIFO of orders (`std::list<Order>` for stable iterators + O(1) erase).
  - Cancel index: `std::unordered_map<OrderId, Locator>` where `Locator` holds side, price, and the
    list iterator, for O(1) cancel lookup.
  - Public methods (stubs): `std::vector<Trade> add(const Order&)`, `bool cancel(OrderId)`,
    `best_bid()`, `best_ask()`, plus a way to inspect the top-N levels for tests/diffing.
- **Done when:** compiles; a test constructs an empty book and asserts no best bid/ask.
- **Commit:** `feat(book): add OrderBook skeleton with add/cancel interface`

### Task 1.3 — The seed: a single crossing trade (start here for logic)
- **Modify:** `src/order_book.cpp`, `tests/test_matching.cpp`.
- **Requirements:** implement just enough matching that a buy limit and a sell limit at the same
  price produce exactly one trade for the min quantity at that price. This is the seed of the whole
  engine.
- **Done when:** test — add sell limit 100 @ 10.00, then buy limit 100 @ 10.00 → exactly one trade,
  qty 100, price 10.00, book empty afterward.
- **Commit:** `feat(match): match a single crossing limit order into one trade`

### Task 1.4 — Resting non-crossing limits
- **Requirements:** a limit that does not cross the spread rests in the correct level, appended to
  the FIFO (time priority). Best bid/ask update correctly.
- **Done when:** tests cover: buy below best ask rests; sell above best bid rests; two orders at the
  same price preserve arrival order at that level.
- **Commit:** `feat(book): rest non-crossing limit orders with FIFO time priority`

### Task 1.5 — Partial fills across multiple levels (walk the book)
- **Requirements:** an aggressive limit that crosses multiple resting levels fills against them in
  price-then-time order until exhausted or no longer crossing; any remainder rests. Emit one `Trade`
  per resting order touched.
- **Done when:** test — book has asks 300@10.01, 200@10.02, 500@10.03; incoming buy 600 @ 10.03
  produces trades 300@10.01, 200@10.02, 100@10.03; 400 remains at 10.03; no remainder rests.
- **Commit:** `feat(match): support partial fills across multiple price levels`

### Task 1.6 — Market orders
- **Requirements:** market order matches against best available on the opposite side until filled or
  book empty; never rests; unfilled remainder is discarded (log it).
- **Done when:** tests cover a full fill, a partial fill that empties the opposite side, and a market
  order into an empty book (no trade, clean handling).
- **Commit:** `feat(orders): add market order support`

### Task 1.7 — IOC (immediate-or-cancel)
- **Requirements:** match whatever crosses right now; discard the remainder instead of resting.
- **Done when:** test — IOC buy that partially fills leaves the rest cancelled, nothing rests.
- **Commit:** `feat(orders): add immediate-or-cancel (IOC) support`

### Task 1.8 — FOK (fill-or-kill)
- **Requirements:** pre-scan the opposite side; execute in full **only if** the entire quantity is
  immediately fillable, otherwise do nothing (no partial, no rest). The pre-check pass is the point.
- **Done when:** tests — FOK that can fully fill executes; FOK that cannot leaves the book untouched
  and emits no trades.
- **Commit:** `feat(orders): add fill-or-kill (FOK) with pre-scan check`

### Task 1.9 — Cancel via O(1) order-id index
- **Requirements:** `cancel(OrderId)` finds the order through the index, unlinks it from its level in
  O(1), and removes the level if it becomes empty. Cancelling an unknown or already-filled id is a
  clean rejection (returns false), never a crash. Keep the index consistent on every add/fill.
- **Done when:** tests — cancel a resting order (level shrinks/empties); cancel unknown id → false;
  cancel an id that was fully filled → false; best bid/ask update after a cancel empties the top.
- **Commit:** `feat(book): add O(1) cancel via order-id index`

### Task 1.10 — Order modify / partial cancel (reduce)
- **Requirements:** support reducing an order's remaining quantity by id, and a modify that may change
  price and/or size (both needed by Phase 2A's MBO `C`/`M` actions and Phase 2B's ITCH `X`/`U`). A
  reduce to zero removes the order. A price change loses time priority — model it as cancel + re-add.
- **Done when:** tests cover reduce-in-place, reduce-to-zero, and a price-changing modify that resets
  priority; index stays consistent.
- **Commit:** `feat(book): support order reduce and modify`

### Task 1.11 — Edge-case hardening
- **Modify:** `tests/test_edge_cases.cpp`.
- **Requirements:** add explicit tests (and any guards they expose) for: exact level exhaustion
  removing the level; zero/negative quantity rejected; self-consistent book after interleaved
  add/cancel/match sequences; never producing a crossed book.
- **Done when:** all edge tests pass; guards are minimal and documented.
- **Commit:** `test(edge): cover level exhaustion, bad input, and interleaving`

### Task 1.12 — Property / invariant tests (fuzz)
- **Modify:** `tests/test_invariants.cpp`.
- **Requirements:** generate long randomized sequences of add/cancel/reduce/match operations. After
  **every** operation assert invariants that must always hold:
  - book is never crossed (best bid < best ask when both exist);
  - no empty price levels linger;
  - the cancel index exactly matches the orders resting in the book;
  - shares are conserved across each trade (aggressor filled == sum of resting reductions).
  Use a fixed seed set for reproducibility; run enough iterations to be meaningful. **Expose the
  invariant checks as a reusable helper** — Phase 2B reuses it over a full ITCH replay.
- **Done when:** thousands of randomized ops run with zero invariant violations.
- **Commit:** `test(invariants): add randomized property tests for book invariants`

### Task 1.13 — Phase 1 design doc
- **Create:** `docs/phase1-design.md` — the data-structure choices (why two ordered maps, why FIFO
  lists, why the integer-price invariant, why the cancel index), the matching algorithm, and the
  testing strategy including the invariants above.
- **Commit:** `docs: document Phase 1 engine design and test strategy`

**Phase 1 gate (must pass before Phase 2):** full suite green, including the invariant tests over
thousands of randomized operations, and scenario tests matching hand-computed trade logs.

---

## 7. Phase 2 — Validation on real market data

Two tracks. **Phase 2A is the primary validation** (Databento MBO drives the engine, MBP-10 is the
reference — same instrument, same session, so they align event-for-event). **Phase 2B is a secondary
systems artifact** (raw ITCH 5.0 binary parsing). Keeping them separate means a book-diff failure is
unambiguously a matching-logic bug, and a parser failure is unambiguously a decode bug.

### Phase 2A — Validate the engine against Databento (MBO → MBP-10)

**Data facts** (see `data/README.md`; pulled by `scripts/pull_databento.py`):
- Dataset `XNAS.ITCH`, **historical** only. Two schemas for the same symbol/session:
  - **MBO** (L3): one record per order action — fields include `order_id`, `price`, `size`,
    `action`, `side`, `ts_event`, `sequence`, `flags`. Drives the engine.
  - **MBP-10** (L2): one record per book-changing event with the top-10 levels (`bid_px`, `ask_px`,
    `bid_sz`, `ask_sz`, `bid_ct`, `ask_ct` per level). The reference to diff against.
- **Prices are `1e-9` fixed-point** (nanodollars). `UNDEF_PRICE` = `INT64_MAX` is the null sentinel.
- **MBO `action` codes:** `A` add, `C` cancel/reduce, `M` modify, `R` clear (book reset), `T` trade
  (print), `F` fill, `N` none. **`side`:** `B` bid, `A` ask, `N` none.
- **Start the pull at session open** (13:30 UTC in EDT / 14:30 UTC in EST for 09:30 ET) so the book
  builds from empty and matches MBP-10 from the first event. Historical MBO does **not** prepend a
  snapshot — a mid-session start means missing resting orders and spurious diffs.

**Symbol choice:** develop against **INTC** (moderate volume — cheap, fast to debug the first
divergence), then run the identical code against **AAPL** as the stress/showcase run.

#### Task 2A.1 — Databento historical pull script
- **Create:** `scripts/pull_databento.py`.
- **Requirements:** a parameterized script (symbol, date, start/end UTC, out dir) that reads
  `DATABENTO_API_KEY` from the environment, **prints the cost estimate first** via
  `metadata.get_cost` and refuses to download if it exceeds a `--max-cost` guard, then fetches both
  schemas to DBN files under `data/databento/`. Uses the **historical** client only. Reference:
  ```python
  import os, databento as db
  client = db.Historical(os.environ["DATABENTO_API_KEY"])
  args = dict(dataset="XNAS.ITCH", symbols=[SYMBOL], stype_in="raw_symbol",
              start=START_UTC, end=END_UTC)          # e.g. "2025-06-04T13:30", "...T20:00"
  for schema in ("mbo", "mbp-10"):
      cost = client.metadata.get_cost(schema=schema, **args)
      print(schema, "estimated cost $", cost)        # gate on --max-cost before downloading
      data = client.timeseries.get_range(schema=schema, **args)
      data.to_file(f"data/databento/{SYMBOL}_{DATE}_{schema}.dbn")
  ```
  Document the chosen session date (the one already cost-checked) in `data/README.md`.
- **Done when:** running the script with the INTC parameters downloads
  `INTC_<date>_mbo.dbn` and `INTC_<date>_mbp-10.dbn` into `data/databento/`.
- **Commit:** `chore(data): add Databento historical pull script for MBO and MBP-10`

#### Task 2A.2 — DBN loaders (C++ via databento-cpp)
- **Create:** `include/ob/dbn/loader.hpp`, `src/dbn/loader.cpp`; add `databento-cpp` to CMake via
  `FetchContent` (DBN decoder path only — do **not** use its live/historical HTTP client).
- **Requirements:** stream-decode a DBN file of `MboMsg` records into a typed event struct
  (`order_id`, i64 `price` at 1e-9, `size`, `action`, `side`, `sequence`, `flags`) and a DBN file of
  `Mbp10Msg` records into a top-10 snapshot struct. Guard `UNDEF_PRICE`. Keep prices integral.
- **Done when:** a unit test decodes a tiny committed DBN fixture (or a synthetic record buffer) into
  the expected typed values for both schemas, including an `UNDEF_PRICE` guard.
- **Commit:** `feat(dbn): decode Databento MBO and MBP-10 DBN files`

#### Task 2A.3 — Map MBO actions onto the engine
- **Modify:** `src/dbn/loader.cpp` (or a `book_builder`), wiring MBO events to Phase 1:
  - `A` → `add` a resting order (`order_id`, side, price, size);
  - `C` → reduce/cancel the referenced order by `size`;
  - `M` → modify the referenced order (price and/or size; a price change loses priority);
  - `F` → reduce/fill the referenced resting order by `size`;
  - `R` → clear the entire book (reset all levels);
  - **`T` (trade) → informational print; for Nasdaq MBO it does not itself modify the book** (the
    paired `F`/`C` records remove the liquidity). Follow Databento's MBO book-reconstruction
    guidance; treat `T` as no book mutation for the level state.
  - `N` → no book action. Skip records whose `price` is `UNDEF_PRICE` where a price is required.
- **Done when:** a scripted sequence of MBO events reproduces the expected book state, asserted
  against the Phase 1 book API.
- **Commit:** `feat(dbn): map MBO order actions to the engine`

#### Task 2A.4 — MBO→book diff harness (runnable app)
- **Create:** `apps/dbn_diff_main.cpp`.
- **Requirements:** replay the MBO file through the engine and compare the reconstructed top-10 book
  to the MBP-10 reference. **Alignment is the critical detail:** MBP-10 emits one record per
  book-changing event while MBO emits one record per order action, so multiple MBO records can belong
  to one event. Apply MBO records in `sequence` order and compare your top-10 to an MBP-10 record only
  at the matching event boundary (use `sequence`/`ts_event`, and the `flags` last-message bit to know
  an event's MBO records are complete). **Do not compare mid-event.** On the first divergence, print
  the sequence/event index and expected-vs-actual top-10; on a clean run, print total events matched.
  No gtest assertion here — this commit is the runnable diff tool. Reads from `data/databento/`.
- **Done when:** the app runs against the INTC pair and prints either a clean match summary or a
  precise first-divergence report.
- **Commit:** `feat(dbn): drive engine from MBO and diff against MBP-10`

#### Task 2A.5 — INTC full-session validation (green)
- **Create/modify:** `tests/dbn/test_book_diff.cpp`.
- **Requirements:** a test that runs the MBO→book build against the **INTC** files and asserts the
  engine's book matches MBP-10 to **10 levels at every event boundary** for the full session. This is
  where real debugging happens — likely culprits: event-boundary alignment, `T` vs `F` handling, the
  1e-9 price scale, priority loss on modify, or a non-session-open start. Fix them in the
  engine/loader, not by loosening the diff. Reads from `data/databento/`; **skips gracefully** with a
  clear message when the files are absent (CI stays green without data). Commit only once green.
- **Done when:** INTC matches to 10 levels across the full session (data present) / skips (absent).
- **Commit:** `test(dbn): validate INTC full session against MBP-10 to 10 levels`

#### Task 2A.6 — AAPL stress / showcase validation
- **Modify:** `tests/dbn/test_book_diff.cpp`.
- **Requirements:** run the same validation against the **AAPL** files (much higher volume). Fix any
  high-volume-only divergences. This is the headline result for the README.
- **Done when:** AAPL matches to 10 levels across the full session (data present) / skips (absent).
- **Commit:** `test(dbn): validate AAPL full session against MBP-10`

### Phase 2B — Raw ITCH 5.0 parser (independent systems artifact)

**Data facts** (confirm exact field offsets against the official NASDAQ TotalView-ITCH 5.0 spec PDF —
treat the spec as source of truth, do not guess byte layouts):
- The stream is **big-endian**. On little-endian x86, byte-swap every multi-byte integer. Getting
  this wrong yields garbage prices — expect to debug it.
- In the BinaryFILE framing, each message is preceded by a **2-byte big-endian length**.
- Price fields are 4-byte unsigned integers in units of **`1e-4`** — store the raw integer. (Note
  this differs from Databento's `1e-9`; never mix the two scales.)
- Timestamps are **6-byte** nanoseconds since midnight.
- `E`/`C`/`X`/`D`/`U` reference orders **by order reference number, no price** — look the price up via
  the order-id index. This exercises Phase 1's indexing.
- Relevant types: `A`/`F` add, `E` executed, `C` executed-with-price, `X` cancel/reduce, `D` delete,
  `U` replace, `S` system event, `R` stock directory (to filter to a target symbol).
- **Data source:** the file already obtained — `08302019.NASDAQ_ITCH50` (gunzip the `.gz`), a full
  main-market Nasdaq day (Aug 30 2019, all symbols). Filter to one liquid ticker (e.g. MSFT) via `R`.
  Large (several GB uncompressed). There is no same-day Databento reference for it, and that's fine —
  this track validates the *parser*, not the book against a reference.

#### Task 2B.1 — ITCH message structs and endian decoding
- **Create:** `include/ob/itch/messages.hpp`, plus an endian-swap utility.
- **Requirements:** POD structs (or a decoding layer) for the message types above, with fields per
  the spec. Provide `read_be<T>()` helpers that decode big-endian integers. Prices decode to the raw
  i64 (1e-4 scale); keep them integral.
- **Done when:** a unit test decodes hand-built big-endian byte buffers into the correct field values
  for at least `A`, `E`, `X`, `D`, `U`.
- **Commit:** `feat(itch): add ITCH 5.0 message structs and big-endian decoding`

#### Task 2B.2 — Binary stream reader with length-prefixed dispatch
- **Create:** `include/ob/itch/parser.hpp`, `src/itch/parser.cpp`.
- **Requirements:** read the 2-byte length prefix, read that many bytes, dispatch on the message type
  byte, and expose a callback/visitor interface (`on_add`, `on_execute`, `on_cancel`, `on_delete`,
  `on_replace`, `on_system`, `on_directory`). Stream from a file without loading it all into memory.
- **Done when:** a parser test feeds a small synthetic ITCH byte stream and asserts the callbacks
  fire in order with correct payloads.
- **Commit:** `feat(itch): add length-prefixed binary parser with message dispatch`

#### Task 2B.3 — Parser unit tests (synthetic fixtures)
- **Modify:** `tests/itch/test_parser.cpp`.
- **Requirements:** small committed **synthetic** byte fixtures (not licensed data) covering
  endianness, each message type, and the length-prefix framing. Verify decoded values exactly.
- **Done when:** parser tests are comprehensive and green.
- **Commit:** `test(itch): add parser unit tests with synthetic fixtures`

#### Task 2B.4 — Parser validation on the NASDAQ ITCH sample
- **Modify:** `tests/itch/test_parser.cpp` (or a dedicated validation test).
- **Requirements:** run the parser over the real `08302019.NASDAQ_ITCH50` file and assert
  **structural sanity** (validates the parser, not the book): every message decodes with no buffer
  overrun; per-type message counts are non-zero and internally consistent; prices/sizes fall in sane
  ranges; `E`/`X`/`D`/`U` order references resolve against previously-added ids. Reads from
  `data/itch/`; **skips gracefully** if absent.
- **Done when:** the parser cleanly traverses the full sample file with all sanity assertions passing
  (data present) / skips (absent).
- **Commit:** `test(itch): validate parser against the NASDAQ ITCH sample`

#### Task 2B.5 — Map ITCH events to the engine + replay driver
- **Create:** `apps/replay_main.cpp`, `include/ob/itch/replay.hpp`, `src/itch/replay.cpp`.
- **Requirements:** wire parser callbacks to the engine — `A`/`F` → add; `E`/`C` → reduce/fill by id;
  `X` → reduce; `D` → cancel; `U` → cancel old id + add new id (new price/size, new time priority).
  Filter to a configurable target symbol via the `R` directory. The CLI takes an ITCH file path and
  symbol, replays it, and can print periodic top-of-book snapshots. Correct replay only — no
  performance work.
- **Done when:** the driver replays the sample for a chosen symbol and prints sane, monotonic
  top-of-book output without crashing.
- **Commit:** `feat(itch): map ITCH events to the engine and add replay driver`

#### Task 2B.6 — Full-replay invariant coherence check
- **Create:** `tests/itch/test_replay_invariants.cpp`.
- **Requirements:** replay the full ITCH sample through the engine and assert the Phase 1 invariants
  (reuse the helper from Task 1.12) hold after every event: book never crossed, no lingering empty
  levels, cancel index consistent. This is the defensible validation of the ITCH→engine path absent a
  same-day reference. Reads from `data/itch/`; **skips gracefully** if absent.
- **Done when:** a full real ITCH session replays with zero invariant violations (present) / skips.
- **Commit:** `test(itch): assert book invariants over a full ITCH replay`

### Phase 2C — Documentation

#### Task 2C.1 — Phase 2 doc and data instructions
- **Create/modify:**
  - `docs/phase2-data.md` — both tracks; why MBO→MBP-10 gives a clean same-day diff; the gotchas
    (1e-9 vs 1e-4 price scales, `UNDEF_PRICE`, event-boundary alignment via `sequence`/`flags`, `T`
    vs `F` handling, session-open start, big-endian ITCH framing); and the results (INTC + AAPL match
    to 10 levels; parser sanity + invariant coherence on the ITCH sample).
  - `data/README.md` — exact instructions:
    - **Databento:** set `DATABENTO_API_KEY`, then run `scripts/pull_databento.py` with the chosen
      symbol/date/window (state the cost-checked session date); files land in `data/databento/`.
    - **ITCH:** the file is `08302019.NASDAQ_ITCH50.gz` from `https://emi.nasdaq.com/ITCH/Nasdaq ITCH/`;
      gunzip it into `data/itch/`. Note it's large and gitignored.
- **Commit:** `docs: document Phase 2 validation tracks and data sources`

**Phase 2 gate:**
- Phase 2A — the engine matches the MBP-10 reference to 10 levels for the full INTC session and the
  full AAPL session, with the diff tests in the suite (skipping cleanly without data).
- Phase 2B — the parser passes structural validation on the NASDAQ ITCH sample, and a full ITCH
  replay holds all Phase 1 invariants, with both tests in the suite (skipping cleanly without data).

---

## 8. Out of scope for this plan (do not build yet)

Phase 3 (performance) comes later and separately: replacing `std::map` with a flat price-level array,
an order object pool / free-list, intrusive lists, cache-line layout, and `perf`-driven before/after
benchmarking. **Do not** start any of this now, and do not pre-optimize Phase 1/2 code in ways that
hurt clarity. Also out of scope entirely: **Databento live data** (use only the historical
`timeseries.get_range` — the live client needs a Nasdaq professional license the project must not
depend on), networking, FIX protocol, a GUI, multithreading in the matching core, and any trading
strategy.

---

## 9. Overall definition of done for this handoff

- Repo matches the layout in §3, builds with CMake, tests run via CTest.
- Phase 1 gate and both Phase 2 gates (2A and 2B) pass; data-dependent tests skip cleanly on a fresh
  clone with no market data, so the default `ctest` run is green.
- Git history is the granular, green, Conventional-Commits sequence defined above — no squashing.
- README, `docs/phase1-design.md`, and `docs/phase2-data.md` are written and accurate.
- No floating-point prices anywhere; price scales never mixed across sources; matching core is
  single-threaded; only historical Databento data is used; no market data or secrets committed.
