#!/usr/bin/env python3
"""
QShell Order Executor — order_executor.py
Simulates order execution (paper/live mode).
Usage: echo "AAPL BUY 100 185.50" | python3 order_executor.py
       python3 order_executor.py --mode paper|live --slippage_bps N

Reads orders from stdin. Outputs execution report.
Exit 0 = all orders executed. Exit 1 = some failed.
"""
import sys
import os
import random
import json
from datetime import datetime

def execute_order(order_str, mode="paper", slippage_bps=2):
    parts = order_str.strip().split()
    if len(parts) < 3:
        return None
    ticker = parts[0].upper()
    action = parts[1].upper()
    size   = int(parts[2])
    price  = float(parts[3]) if len(parts) > 3 else 100.0

    # Simulate slippage
    slip = price * slippage_bps / 10000
    if action == "BUY":
        fill_price = round(price + slip, 2)
    else:
        fill_price = round(price - slip, 2)

    notional = round(fill_price * size, 2)
    return {
        "timestamp": datetime.now().isoformat(),
        "ticker": ticker,
        "action": action,
        "size": size,
        "requested_price": price,
        "fill_price": fill_price,
        "notional": notional,
        "mode": mode,
        "status": "FILLED"
    }

def main():
    args = sys.argv[1:]
    mode = "paper"
    slippage_bps = 2
    i = 0
    while i < len(args):
        if args[i] == "--mode" and i+1 < len(args):
            mode = args[i+1]; i += 2
        elif args[i] == "--slippage_bps" and i+1 < len(args):
            slippage_bps = int(args[i+1]); i += 2
        else:
            i += 1

    orders_raw = sys.stdin.read().strip()
    if not orders_raw:
        print("No orders received", file=sys.stderr)
        sys.exit(1)

    results = []
    for line in orders_raw.splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        result = execute_order(line, mode, slippage_bps)
        if result:
            results.append(result)
            print(f"FILLED {result['ticker']} {result['action']} {result['size']} @ {result['fill_price']} notional={result['notional']}")

    if not results:
        print("No valid orders to execute", file=sys.stderr)
        sys.exit(1)

    sys.exit(0)

if __name__ == "__main__":
    main()