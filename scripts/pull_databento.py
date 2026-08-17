#!/usr/bin/env python3
"""Pull historical MBO and MBP-10 data from Databento for a given symbol/session.

Usage:
    python scripts/pull_databento.py --symbol INTC --date 2025-06-04 \
        --start 2025-06-04T13:30 --end 2025-06-04T20:00 --max-cost 5.0

Requires DATABENTO_API_KEY in the environment (see .env.example).
"""

import argparse
import os
import sys

import databento as db


def main():
    parser = argparse.ArgumentParser(description="Pull Databento MBO + MBP-10 data")
    parser.add_argument("--symbol", required=True, help="Raw symbol (e.g. INTC, AAPL)")
    parser.add_argument("--date", required=True, help="Session date (e.g. 2025-06-04)")
    parser.add_argument("--start", required=True, help="Start UTC (e.g. 2025-06-04T13:30)")
    parser.add_argument("--end", required=True, help="End UTC (e.g. 2025-06-04T20:00)")
    parser.add_argument("--max-cost", type=float, default=5.0,
                        help="Max cost in USD per schema before refusing (default 5.0)")
    parser.add_argument("--out-dir", default="data/databento",
                        help="Output directory (default: data/databento)")
    args = parser.parse_args()

    api_key = os.environ.get("DATABENTO_API_KEY")
    if not api_key:
        print("ERROR: DATABENTO_API_KEY not set. See .env.example.", file=sys.stderr)
        sys.exit(1)

    client = db.Historical(api_key)
    os.makedirs(args.out_dir, exist_ok=True)

    common = dict(
        dataset="XNAS.ITCH",
        symbols=[args.symbol],
        stype_in="raw_symbol",
        start=args.start,
        end=args.end,
    )

    for schema in ("mbo", "mbp-10"):
        print(f"\n--- {schema} ---")
        cost = client.metadata.get_cost(schema=schema, **common)
        print(f"Estimated cost: ${cost:.4f}")

        if cost > args.max_cost:
            print(f"REFUSED: cost ${cost:.4f} exceeds --max-cost ${args.max_cost:.2f}",
                  file=sys.stderr)
            sys.exit(1)

        out_path = os.path.join(args.out_dir,
                                f"{args.symbol}_{args.date}_{schema}.dbn.zst")
        print(f"Downloading to {out_path} ...")
        data = client.timeseries.get_range(schema=schema, **common)
        data.to_file(out_path)
        print(f"Saved {out_path}")

    print("\nDone.")


if __name__ == "__main__":
    main()
