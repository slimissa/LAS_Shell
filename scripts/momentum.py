#!/usr/bin/env python3
"""
QShell Momentum Engine — momentum.py
Computes momentum signals for a list of tickers.
Usage: python3 momentum.py TICKER1 TICKER2 ... [--lookback N] [--topn N]
       python3 momentum.py --watchlist FILE [--topn 3]
Outputs: TICKER SIGNAL SCORE  (one per line)
SIGNAL: BUY | SELL | HOLD
Exit 0 if at least one signal generated, exit 1 if no signals.
"""
import sys
import os
import random
import hashlib
from datetime import date

SEED_PRICES = {
    "AAPL": 185.0, "MSFT": 415.0, "GOOGL": 175.0, "AMZN": 195.0,
    "TSLA": 175.0, "META": 510.0, "NVDA": 875.0, "SPY":  510.0,
    "QQQ":  435.0, "GLD":  195.0,
}

def get_returns(ticker, lookback=20):
    """Simulate lookback-day return for ticker."""
    seed = int(hashlib.md5(f"{ticker}{date.today()}momentum".encode()).hexdigest(), 16) % 9999
    random.seed(seed)
    returns = [random.gauss(0.0003, 0.015) for _ in range(lookback)]
    cum = 1.0
    for r in returns:
        cum *= (1 + r)
    return cum - 1.0

def main():
    args = sys.argv[1:]
    lookback = 20
    topn = None
    watchlist_file = None
    tickers = []
    i = 0
    while i < len(args):
        if args[i] == "--lookback" and i+1 < len(args):
            lookback = int(args[i+1]); i += 2
        elif args[i] == "--topn" and i+1 < len(args):
            topn = int(args[i+1]); i += 2
        elif args[i] == "--watchlist" and i+1 < len(args):
            watchlist_file = args[i+1]; i += 2
        else:
            tickers.append(args[i].upper()); i += 1

    if watchlist_file:
        try:
            with open(watchlist_file) as f:
                tickers += [l.strip().upper() for l in f if l.strip() and not l.startswith('#')]
        except FileNotFoundError:
            print(f"watchlist not found: {watchlist_file}", file=sys.stderr)

    if not tickers:
        tickers = list(SEED_PRICES.keys())

    scored = []
    for t in tickers:
        ret = get_returns(t, lookback)
        if ret > 0.02:
            signal = "BUY"
        elif ret < -0.02:
            signal = "SELL"
        else:
            signal = "HOLD"
        scored.append((t, signal, round(ret * 100, 2)))

    scored.sort(key=lambda x: x[2], reverse=True)
    if topn:
        scored = scored[:topn]

    if not scored:
        sys.exit(1)

    for t, sig, score in scored:
        print(f"{t} {sig} {score:+.2f}%")

    sys.exit(0)

if __name__ == "__main__":
    main()