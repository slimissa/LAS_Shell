#!/usr/bin/env python3
"""Verify buy/sell balance in execution log."""
import os, sys
logdir    = os.environ.get("LOGDIR", "logs")
exec_file = f"{logdir}/executions.csv"
if not os.path.exists(exec_file):
    print("No executions to verify"); sys.exit(0)
buys = sells = 0
with open(exec_file) as f:
    for line in f:
        if "BUY" in line:  buys  += 1
        if "SELL" in line: sells += 1
ratio = buys / max(sells, 1)
print(f"Buy legs: {buys}  Sell legs: {sells}  Ratio: {ratio:.2f}")
if 0.8 <= ratio <= 1.2:
    print("✔ Hedge ratio balanced")
else:
    print("⚠ Hedge ratio imbalanced — review positions")