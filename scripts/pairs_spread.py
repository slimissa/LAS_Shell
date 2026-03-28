#!/usr/bin/env python3
"""
QShell Pairs Spread Engine — pairs_spread.py
Computes the z-score spread between two correlated assets.
Usage: python3 pairs_spread.py TICKER_A TICKER_B [--lookback N] [--entry Z] [--exit Z]
Outputs: SPREAD ZSCORE SIGNAL  (LONG_A_SHORT_B | LONG_B_SHORT_A | EXIT | HOLD)
Exit 0 = actionable signal. Exit 1 = hold/no signal. Exit 2 = error.
"""
import sys
import random
import hashlib
import math
from datetime import date

def sim_spread_zscore(ticker_a, ticker_b, lookback=60):
    seed = int(hashlib.md5(f"{ticker_a}{ticker_b}{date.today()}".encode()).hexdigest(), 16) % 9999
    random.seed(seed)
    # Simulate correlated price series
    spreads = []
    price_a, price_b = 100.0, 100.0
    beta = random.uniform(0.8, 1.2)
    for _ in range(lookback):
        common = random.gauss(0, 0.01)
        price_a *= (1 + common + random.gauss(0, 0.005))
        price_b *= (1 + common * beta + random.gauss(0, 0.005))
        spreads.append(price_a - beta * price_b)
    mean = sum(spreads) / len(spreads)
    std  = math.sqrt(sum((s - mean)**2 for s in spreads) / len(spreads))
    current_spread = spreads[-1]
    zscore = (current_spread - mean) / std if std > 0 else 0.0
    return round(current_spread, 4), round(zscore, 3), round(mean, 4), round(std, 4)

def main():
    args = sys.argv[1:]
    lookback = 60
    entry_z  = 2.0
    exit_z   = 0.5
    tickers  = []
    i = 0
    while i < len(args):
        if args[i] == "--lookback" and i+1 < len(args):
            lookback = int(args[i+1]); i += 2
        elif args[i] == "--entry" and i+1 < len(args):
            entry_z = float(args[i+1]); i += 2
        elif args[i] == "--exit" and i+1 < len(args):
            exit_z = float(args[i+1]); i += 2
        else:
            tickers.append(args[i].upper()); i += 1

    if len(tickers) < 2:
        print("Usage: pairs_spread.py TICKER_A TICKER_B [--lookback N] [--entry Z] [--exit Z]", file=sys.stderr)
        sys.exit(2)

    a, b = tickers[0], tickers[1]
    spread, zscore, mean, std = sim_spread_zscore(a, b, lookback)

    if zscore > entry_z:
        signal = f"LONG_{b}_SHORT_{a}"
        exit_code = 0
    elif zscore < -entry_z:
        signal = f"LONG_{a}_SHORT_{b}"
        exit_code = 0
    elif abs(zscore) < exit_z:
        signal = "EXIT"
        exit_code = 0
    else:
        signal = "HOLD"
        exit_code = 1

    print(f"PAIR={a}/{b} SPREAD={spread} ZSCORE={zscore} MEAN={mean} STD={std} SIGNAL={signal}")
    sys.exit(exit_code)

if __name__ == "__main__":
    main()