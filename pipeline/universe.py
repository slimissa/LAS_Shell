#!/usr/bin/env python3
"""
Las_shell Pipeline Stage 1 — universe.py
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

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts"))
try:
    import live_price_feed  # noqa: E402  — Milestone 1 (v0.6.0) real feed
    _HAVE_LIVE_FEED = True
except ImportError:
    _HAVE_LIVE_FEED = False  # scripts/ not on path for some reason — degrade to synthetic

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
    """Synthetic path — kept as the documented fallback. Used directly
    when LAS_SHELL_LIVE_FEED=0, and per-symbol whenever the live feed
    can't get a real price for that symbol (see get_live_prices below)."""
    seed = int(hashlib.md5(f"{symbol}{date.today()}".encode()).hexdigest(), 16) % 9999
    random.seed(seed)
    base  = SEED_PRICES.get(symbol.upper(), 100.0)
    noise = random.gauss(0, 0.008)
    return round(base * (1 + noise), 2)


def get_live_prices(symbols):
    """Milestone 1 (v0.6.0): fetch real prices through the shared cache
    in scripts/live_price_feed.py — the same cache pipeline/src/universe.c
    reads, so both language paths consume identical real data instead of
    diverging synthetic RNGs. Returns {symbol: entry} where entry always
    has 'price', 'source' ('stooq' or 'synthetic_fallback'), and
    'fetch_status'. Never raises — a total feed outage degrades every
    symbol to synthetic_fallback rather than crashing universe.py.
    """
    if not _HAVE_LIVE_FEED:
        return {s: {**get_price_dict_fallback(s), "source": "synthetic_fallback",
                     "fetch_status": "n/a:module_unavailable"} for s in symbols}
    try:
        return live_price_feed.get_prices(symbols)
    except Exception:  # noqa: BLE001 — feed must never take the pipeline down
        return {s: {**get_price_dict_fallback(s), "source": "synthetic_fallback",
                     "fetch_status": "n/a:exception"} for s in symbols}


def get_price_dict_fallback(symbol):
    price = get_price(symbol)
    return {"price": price, "timestamp": datetime.now().isoformat()}


def make_candidate(symbol, live_entry=None):
    if live_entry is not None:
        price = live_entry["price"]
        data_source = live_entry.get("source", "synthetic_fallback")
        fetch_status = live_entry.get("fetch_status", "n/a")
    else:
        price = get_price(symbol)
        data_source = "synthetic"
        fetch_status = "n/a"
    return {
        "symbol": symbol.upper(),
        "signal": 0.0,          # filled by momentum_filter
        "size":   0,            # filled by size_positions
        "price":  price,
        "side":   "BUY",
        "meta": {
            "_convention": "1.0",
            "strategy":    "las_shell_pipeline",
            "stage":       "universe",
            "timestamp":   datetime.now().isoformat(),
            "data_source": data_source,   # "stooq" | "synthetic_fallback" | "synthetic"
            "fetch_status": fetch_status,
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

    # Milestone 1 (v0.6.0): live feed is the default. Set
    # LAS_SHELL_LIVE_FEED=0 to use the pure-synthetic path documented as
    # the fallback (also used automatically per-symbol if the real fetch
    # fails for that symbol — see get_live_prices()).
    use_live = os.environ.get("LAS_SHELL_LIVE_FEED", "1") != "0"

    if use_live:
        live_entries = get_live_prices(symbols)
        candidates = [make_candidate(s, live_entries.get(s.upper())) for s in symbols]
        n_real = sum(1 for e in live_entries.values() if e.get("source") == "stooq")
        n_fallback = len(live_entries) - n_real
        print(f"[universe] live feed: {n_real} real, {n_fallback} synthetic_fallback",
              file=sys.stderr)
    else:
        candidates = [make_candidate(s) for s in symbols]

    print(json.dumps(candidates, indent=2))
    print(f"[universe] generated {len(candidates)} candidates", file=sys.stderr)
    sys.exit(0)

if __name__ == "__main__":
    main()