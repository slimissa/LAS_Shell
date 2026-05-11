#!/usr/bin/env python3
"""
quote — Las_shell price feed for $<() streaming substitution.

Usage:
    quote TICKER                  # print one price and exit (one-shot mode)
    quote TICKER --stream         # print a new price every second, indefinitely
    quote TICKER --stream N       # print N prices then exit
    quote TICKER --field FIELD    # select field: close|open|high|low|volume

This is the canonical command for $<() streaming substitution:

    # Stream AAPL price into $price each loop iteration:
    while price=$<(quote AAPL --stream); do
        assert $price > 0
        echo "AAPL: $price"
        sleep 1
    done

    # One-shot: read a single price
    price=$<(quote AAPL)
    echo "Current AAPL price: $price"

Design: In streaming mode, the process writes one price per line to stdout,
pausing 1 second between lines. The $<() FIFO reader consumes one line per
loop iteration, blocking until the next line arrives. This creates a
natural 1-second heartbeat for the trading loop.

The price simulation is deterministic per (ticker, minute) so that adjacent
calls return the same "current" price within the same minute.
"""

import sys
import os
import random
import math
import time
import hashlib
from datetime import datetime

# ── Base prices (approximate real-world anchors) ──
BASE_PRICES = {
    "AAPL":  185.0,  "MSFT":  415.0,  "GOOGL": 175.0,  "AMZN":  195.0,
    "TSLA":  175.0,  "META":  510.0,  "NVDA":  875.0,  "SPY":   510.0,
    "QQQ":   435.0,  "GLD":   195.0,  "IWM":   200.0,  "BTC":   67000.0,
    "ETH":   3500.0, "NFLX":  620.0,  "PYPL":   65.0,  "AMD":   165.0,
    "INTC":   30.0,  "JPM":   195.0,  "GS":    465.0,  "BAC":    37.0,
}

def sim_price(ticker: str, field: str = "close", noise_seed: int = 0) -> float:
    """
    Simulate a realistic price for ticker using a seeded random walk.
    noise_seed allows generating successive different values for streaming.
    """
    ticker = ticker.upper()
    base   = BASE_PRICES.get(ticker, 100.0)

    # Deterministic daily drift based on ticker + date
    day_seed = int(hashlib.md5(f"{ticker}{datetime.now().strftime('%Y%m%d')}".encode()).hexdigest(), 16)
    random.seed(day_seed)
    daily_ret = random.gauss(0.0002, 0.012)
    close_base = base * (1.0 + daily_ret)

    # Intraday micro-noise seeded by (ticker, minute, noise_seed)
    min_seed = int(hashlib.md5(
        f"{ticker}{datetime.now().strftime('%Y%m%d%H%M')}{noise_seed}".encode()
    ).hexdigest(), 16)
    random.seed(min_seed)
    intraday = random.gauss(0, 0.002)
    close = round(close_base * (1.0 + intraday), 2)

    if field == "close": return close
    random.seed(min_seed + 1)
    if field == "open":   return round(close * random.uniform(0.997, 1.003), 2)
    if field == "high":   return round(close * random.uniform(1.001, 1.015), 2)
    if field == "low":    return round(close * random.uniform(0.985, 0.999), 2)
    if field == "volume": return int(random.uniform(5e6, 80e6))
    return close

def main():
    args = sys.argv[1:]
    if not args:
        print("Usage: quote TICKER [--stream [N]] [--field FIELD]", file=sys.stderr)
        sys.exit(1)

    ticker    = None
    stream    = False
    count     = None     # None = infinite
    field     = "close"
    interval  = 1.0      # seconds between stream ticks

    i = 0
    while i < len(args):
        a = args[i]
        if a == "--stream":
            stream = True
            # Optional count immediately after --stream
            if i + 1 < len(args) and args[i+1].isdigit():
                count = int(args[i+1]); i += 1
        elif a == "--field" and i + 1 < len(args):
            field = args[i+1]; i += 1
        elif a == "--interval" and i + 1 < len(args):
            interval = float(args[i+1]); i += 1
        elif not a.startswith("--"):
            ticker = a.upper()
        i += 1

    if not ticker:
        print("quote: missing ticker symbol", file=sys.stderr)
        sys.exit(1)

    if not stream:
        # ── One-shot mode ──
        price = sim_price(ticker, field, noise_seed=0)
        print(price, flush=True)
        sys.exit(0)

    # ── Streaming mode ──
    iteration = 0
    try:
        while count is None or iteration < count:
            price = sim_price(ticker, field, noise_seed=iteration)
            print(price, flush=True)
            iteration += 1
            if count is None or iteration < count:
                time.sleep(interval)
    except (BrokenPipeError, KeyboardInterrupt):
        # FIFO read-end closed (shell loop exited) or Ctrl+C — exit cleanly
        sys.exit(0)

if __name__ == "__main__":
    main()
