# CLAUDE.md — order-book project rules

C++ limit order book + matching engine, validated on real market data. The full task-by-task
sequence lives in `order-book-build-plan.md` — **follow it in order**; this file is the standing
rules that apply to every task and every session. When a task and this file agree, this file wins on
conventions; the plan wins on what to build next.

## Non-negotiable invariants

- **Integer prices only. Never floating point.** Price is a signed 64-bit integer.
- **Never mix price scales.** Databento MBO/MBP-10 prices are `1e-9` (nanodollars); NASDAQ ITCH
  prices are `1e-4`. Store each in its native scale; never compare or combine across scales. Guard
  Databento's `UNDEF_PRICE` (`INT64_MAX`) sentinel explicitly.
- **Single-threaded matching core.** No threads or locks in the engine. Do not "scale it up."
- **C++20, RAII.** No raw `new`/`delete` in application code.
- **Historical Databento only.** Use `timeseries.get_range`. Never the live client — it needs a
  Nasdaq professional license this project must not depend on.

## Secrets

- `DATABENTO_API_KEY` comes from the environment (gitignored `.env`; `.env.example` is committed).
- Never hardcode or commit the key, or commit any market data (`data/databento/*`, `data/itch/*`,
  `*.dbn`, `*.gz`, `*.NASDAQ_ITCH50`).

## Build / tooling

- CMake (>= 3.20); GoogleTest via CTest.
- DBN decode via `databento-cpp` (FetchContent) — **decoder path only**, not its live/historical
  HTTP client. The Python `databento` package is used **only** by `scripts/pull_databento.py`.

## Commit discipline (recruiter-facing history — treat as a deliverable)

- **One commit per task**, using the **exact** message the plan specifies.
- **Every commit builds green and passes `ctest`.** Run the full suite before committing. Never
  commit a red build.
- Conventional Commits (`type(scope): summary`), scopes: `core`, `book`, `match`, `orders`, `dbn`,
  `itch`, `replay`, `chore`, `docs`, `test`.
- Write a real commit body when the work involved a decision or a debugged divergence — say what and
  why. Do **not** squash; the granularity is the point.
- Do NOT add Claude co-authorship to git commits. 

## Testing

- Tests that need large external data (Databento DBN, ITCH sample) **must skip gracefully with a
  clear message when the file is absent** — never fail. A fresh clone with no data must run green.
- Reuse the invariant checker from the fuzz tests (Task 1.12) for the ITCH full-replay coherence
  check — don't reimplement it.

## Silent-bug traps (the ones that waste hours)

- **MBO→MBP-10 diff aligns on event boundaries, not per message.** MBP-10 = one record per event;
  MBO = one record per order action. Apply MBO in `sequence` order and compare only at the matching
  event boundary (`sequence`/`ts_event` + the last-message flag). Never compare mid-event.
- **Pull Databento from session open** so the book builds from empty; historical MBO has no prepended
  snapshot, and a mid-day start causes phantom divergences.
- **MBO `T` (trade) and `F` (fill) do not mutate the book** for Nasdaq; only the paired `C` record
  removes liquidity. Apply `A`/`C`/`M`/`R`; treat `T` and `F` as informational. `F`'s price field
  is the trade price, not the resting price. `T` FLAG_LAST events don't produce MBP-10 records.
- **A price-changing modify loses time priority** — model it as cancel + re-add (MBO `M`, ITCH `U`).
- **ITCH is big-endian** and length-prefixed (2-byte BE length per message); byte-swap every
  multi-byte field. `E`/`C`/`X`/`D`/`U` reference orders by id with no price — look price up via the
  order-id index.

## Out of scope (do not build)

Phase 3 performance work (flat arrays, object pools, intrusive lists, `perf` benchmarking),
networking, FIX, GUI, multithreading in the matching core, any trading strategy, and Databento live
data. Do not pre-optimize Phase 1/2 code in ways that hurt clarity.
