#!/usr/bin/env python3
"""
Las_shell Price Feed — price_feed.py
Fetches/simulates OHLCV data for a ticker.
Usage: python3 price_feed.py TICKER [--field close|open|high|low|volume]
       python3 price_feed.py AAPL MSFT --field close   (multi-ticker)
Outputs one value per line (or JSON with --json).
"""
import sys
import os
import json
import random
import math
import time
import hashlib
from datetime import datetime, date

SEED_PRICES = {
    "AAPL": 185.0, "MSFT": 415.0, "GOOGL": 175.0, "AMZN": 195.0,
    "TSLA": 175.0, "META": 510.0, "NVDA": 875.0, "SPY":  510.0,
    "QQQ":  435.0, "GLD":  195.0, "BTC":  67000.0,"ETH":  3500.0,
}

def sim_price(ticker, field="close"):
    seed = int(hashlib.md5(f"{ticker}{date.today()}".encode()).hexdigest(), 16) % 10000
    random.seed(seed)
    base = SEED_PRICES.get(ticker.upper(), 100.0)
    daily_ret = random.gauss(0.0003, 0.015)
    close = round(base * (1 + daily_ret), 2)
    high  = round(close * random.uniform(1.001, 1.02), 2)
    low   = round(close * random.uniform(0.98, 0.999), 2)
    open_ = round(close * random.uniform(0.995, 1.005), 2)
    vol   = int(random.uniform(5e6, 80e6))
    fields = {"close": close, "open": open_, "high": high, "low": low, "volume": vol}
    return fields.get(field, close), close, open_, high, low, vol

def main():
    args = sys.argv[1:]
    field = "close"
    use_json = False
    tickers = []
    i = 0
    while i < len(args):
        if args[i] == "--field" and i+1 < len(args):
            field = args[i+1]; i += 2
        elif args[i] == "--json":
            use_json = True; i += 1
        else:
            tickers.append(args[i].upper()); i += 1

    if not tickers:
        print("Usage: price_feed.py TICKER [--field close|open|high|low|volume] [--json]", file=sys.stderr)
        sys.exit(1)

    results = []
    for t in tickers:
        val, close, open_, high, low, vol = sim_price(t, field)
        if use_json:
            results.append({"ticker": t, "close": close, "open": open_,
                            "high": high, "low": low, "volume": vol,
                            "timestamp": datetime.now().isoformat()})
        else:
            print(f"{t} {val}")

    if use_json:
        print(json.dumps(results, indent=2))

if __name__ == "__main__":
    main()