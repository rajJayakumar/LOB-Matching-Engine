# Market Data

All data files in this directory are gitignored — they are large and/or licensed.
Tests that depend on them skip gracefully when the files are absent.

## Databento (Phase 2A)

1. Set `DATABENTO_API_KEY` in `.env` (see `.env.example`).
2. Run the pull script:
   ```bash
   python scripts/pull_databento.py --symbol INTC --date 2025-06-04 \
       --start 2025-06-04T13:30 --end 2025-06-04T20:00 --max-cost 5.0
   ```
3. Files land in `data/databento/` (e.g. `INTC_2025-06-04_mbo.dbn.zst`).

**Cost-checked session:** 2025-06-04 (INTC), 13:30–20:00 UTC (session open to close).
Start at session open so the book builds from empty — historical MBO has no snapshot.

For AAPL stress validation, repeat with `--symbol AAPL` (same date/window).

## NASDAQ ITCH 5.0 (Phase 2B)

1. Download `08302019.NASDAQ_ITCH50.gz` from
   `https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/`.
2. Gunzip into `data/itch/`:
   ```bash
   mkdir -p data/itch
   gunzip -k 08302019.NASDAQ_ITCH50.gz
   mv 08302019.NASDAQ_ITCH50 data/itch/
   ```
   The uncompressed file is several GB.
