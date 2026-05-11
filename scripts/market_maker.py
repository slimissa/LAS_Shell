#!/usr/bin/env python3
"""
Las_shell Market Maker Engine — market_maker.py
Generates bid/ask quotes around mid-price with dynamic spread.
Usage: python3 market_maker.py TICKER [--size N] [--spread_bps N] [--max_pos N]
Outputs: TICKER BID ASK MID SPREAD_BPS SIZE INVENTORY
Exit 0 = quote generated. Exit 1 = skipping (inventory too large / risk).
"""
import os
import sys
import random
import hashlib
import math
from datetime import date, datetime

SEED_PRICES = {
    "AAPL": 185.0, "MSFT": 415.0, "GOOGL": 175.0, "TSLA": 175.0,
    "META": 510.0, "NVDA": 875.0, "SPY":  510.0,  "QQQ":  435.0,
}

def load_inventory(ticker):
    inv_file = f"/tmp/las_shell_inventory_{ticker}.txt"
    try:
        with open(inv_file) as f:
            return int(f.read().strip())
    except:
        return 0

def save_inventory(ticker, inventory):
    inv_file = f"/tmp/las_shell_inventory_{ticker}.txt"
    with open(inv_file, "w") as f:
        f.write(str(inventory))

def sim_mid(ticker):
    seed = int(hashlib.md5(f"{ticker}{date.today()}{datetime.now().hour}".encode()).hexdigest(), 16) % 9999
    random.seed(seed)
    base = SEED_PRICES.get(ticker.upper(), 100.0)
    noise = random.gauss(0, 0.003)
    return round(base * (1 + noise), 2)

def main():
    args = sys.argv[1:]
    size = 100
    spread_bps = 5
    max_pos = 500
    ticker = None
    i = 0
    while i < len(args):
        if args[i] == "--size" and i+1 < len(args):
            size = int(args[i+1]); i += 2
        elif args[i] == "--spread_bps" and i+1 < len(args):
            spread_bps = int(args[i+1]); i += 2
        elif args[i] == "--max_pos" and i+1 < len(args):
            max_pos = int(args[i+1]); i += 2
        else:
            ticker = args[i].upper(); i += 1

    if not ticker:
        print("Usage: market_maker.py TICKER [--size N] [--spread_bps N] [--max_pos N]", file=sys.stderr)
        sys.exit(1)

    mid = sim_mid(ticker)
    inventory = load_inventory(ticker)

    # Skew spread based on inventory to manage risk
    inv_skew = inventory / max_pos  # -1 to +1
    half_spread = mid * spread_bps / 10000 / 2
    bid = round(mid - half_spread * (1 + inv_skew), 2)
    ask = round(mid + half_spread * (1 - inv_skew), 2)
    actual_bps = round((ask - bid) / mid * 10000, 1)

    # Skip if inventory too large
    if abs(inventory) >= max_pos:
        print(f"TICKER={ticker} SKIPPING inventory={inventory} exceeds max_pos={max_pos}", file=sys.stderr)
        sys.exit(1)

    # Simulate a counterparty crossing the spread — randomly fills either
    # the bid (we buy = inventory +size) or ask (we sell = inventory -size).
    # Uses the actual quoted prices for realised P&L tracking.
    call_count = int(os.environ.get("CYCLE", "1"))
    random.seed(call_count * 7 + int(mid * 100))
    if random.random() < 0.5:
        # Bid was hit — we bought at our bid price
        save_inventory(ticker, inventory + size)
        fill_side, fill_price = "BID", bid
    else:
        # Ask was lifted — we sold at our ask price
        save_inventory(ticker, inventory - size)
        fill_side, fill_price = "ASK", ask

    print(f"TICKER={ticker} BID={bid} ASK={ask} MID={mid} SPREAD_BPS={actual_bps} "
          f"SIZE={size} INVENTORY={inventory} FILL={fill_side}@{fill_price}")
    sys.exit(0)

if __name__ == "__main__":
    main()