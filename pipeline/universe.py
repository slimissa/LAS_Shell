#!/usr/bin/env python3
"""
QShell Pipeline Stage 1 — universe.py
======================================
SOURCE stage: generates initial candidate list from watchlist/screener.
Reads nothing from stdin (or accepts an optional seed list).
Writes a JSON array of order candidates to stdout.

Usage:
    python3 universe.py
    python3 universe.py --watchlist config/watchlist.txt
    python3 universe.py --symbols AAPL,MSFT,GOOGL
    python3 universe.py --top 20          # top 20 by market cap
    echo '[]' | python3 universe.py       # stdin ignored by source stage
"""
import sys
import os
import json
import random
import hashlib
from datetime import date, datetime

# ── Default universe ─────────────────────────────────────────────
DEFAULT_SYMBOLS = [
    "AAPL", "MSFT", "GOOGL", "AMZN", "NVDA",
    "META",  "TSLA", "SPY",   "QQQ",  "GLD",
]

SEED_PRICES = {
    "AAPL": 185.0, "MSFT": 415.0, "GOOGL": 175.0, "AMZN": 195.0,
    "NVDA": 875.0, "META": 510.0,  "TSLA":  175.0, "SPY":  510.0,
    "QQQ":  435.0, "GLD":  195.0,
}

def get_price(symbol):
    seed = int(hashlib.md5(f"{symbol}{date.today()}".encode()).hexdigest(), 16) % 9999
    random.seed(seed)
    base  = SEED_PRICES.get(symbol.upper(), 100.0)
    noise = random.gauss(0, 0.008)
    return round(base * (1 + noise), 2)

def make_candidate(symbol):
    price = get_price(symbol)
    return {
        "symbol": symbol.upper(),
        "signal": 0.0,          # filled by momentum_filter
        "size":   0,            # filled by size_positions
        "price":  price,
        "side":   "BUY",
        "meta": {
            "_convention": "1.0",
            "strategy":    "qshell_pipeline",
            "stage":       "universe",
            "timestamp":   datetime.now().isoformat(),
        }
    }

def main():
    args = sys.argv[1:]
    symbols   = list(DEFAULT_SYMBOLS)
    top_n     = None
    wl_file   = None
    i = 0
    while i < len(args):
        if args[i] == "--symbols" and i+1 < len(args):
            symbols = [s.strip().upper() for s in args[i+1].split(",")]; i += 2
        elif args[i] == "--watchlist" and i+1 < len(args):
            wl_file = args[i+1]; i += 2
        elif args[i] == "--top" and i+1 < len(args):
            top_n = int(args[i+1]); i += 2
        else:
            i += 1

    # Load from watchlist file if provided
    if wl_file:
        try:
            with open(wl_file) as f:
                symbols = [l.strip().upper() for l in f
                           if l.strip() and not l.startswith("#")]
        except FileNotFoundError:
            print(f"[universe] watchlist not found: {wl_file}", file=sys.stderr)
            sys.exit(1)

    if top_n:
        symbols = symbols[:top_n]

    candidates = [make_candidate(s) for s in symbols]

    print(json.dumps(candidates, indent=2))
    print(f"[universe] generated {len(candidates)} candidates", file=sys.stderr)
    sys.exit(0)

if __name__ == "__main__":
    main()