#!/usr/bin/env python3
"""Verify buy/sell balance in execution log."""
import os, sys, csv

logdir    = os.environ.get("LOGDIR", "logs")
exec_file = f"{logdir}/executions.csv"
# FIX MR5: nothing in any real strategy writes to "executions.csv" --
# they all write to "trades.csv" / "pairs_trades.csv" via order_executor.py.
# This check was always silently hitting the early-exit below in practice.
# Allow the caller to point this at the actual trade log.
args = sys.argv[1:]
i = 0
while i < len(args):
    if args[i] == "--log" and i + 1 < len(args):
        exec_file = args[i + 1]
        i += 2
    else:
        i += 1

if not os.path.exists(exec_file):
    print("No executions to verify"); sys.exit(0)

buys = sells = 0
with open(exec_file, newline="") as f:
    for row in csv.reader(f):
        if not row:
            continue
        # row[0] = timestamp, row[1] = "FILLED AAPL BUY 100 @ 185.57 notional=..."
        # (as written by order_executor.py). Whitespace-tokenize that field
        # and check the actual ACTION token, not a substring of the whole line.
        status_field = row[1] if len(row) > 1 else row[0]
        tokens = status_field.split()
        if len(tokens) < 3 or tokens[0] != "FILLED":
            continue  # skip REJECTED / malformed lines -- not actually executed
        action = tokens[2]
        if action == "BUY":
            buys += 1
        elif action == "SELL":
            sells += 1

ratio = buys / max(sells, 1)
print(f"Buy legs: {buys}  Sell legs: {sells}  Ratio: {ratio:.2f}")
if 0.8 <= ratio <= 1.2:
    print("✔ Hedge ratio balanced")
else:
    print("⚠ Hedge ratio imbalanced — review positions")