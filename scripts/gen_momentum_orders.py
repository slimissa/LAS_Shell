#!/usr/bin/env python3
"""Generate sized orders from momentum signals CSV."""
import os, sys, subprocess

logdir  = os.environ.get("LOGDIR", "logs")
capital = float(os.environ.get("CAPITAL", 100000))
lhome   = os.environ.get("LAS_SHELL_HOME", ".")
sig_file = f"{logdir}/momentum_signals.csv"

if not os.path.exists(sig_file):
    print(f"No signals file: {sig_file}", file=sys.stderr)
    sys.exit(1)

orders = []
with open(sig_file) as f:
    for line in f:
        parts = line.strip().split(",", 1)
        data  = parts[-1].strip().split()
        if len(data) < 2: continue
        ticker, signal = data[0], data[1]
        if signal not in ("BUY", "SELL"): continue
        # Defense in depth: even with subprocess.run (no shell), a ticker
        # containing spaces or control characters could still corrupt the
        # downstream space-separated "TICKER ACTION SIZE PRICE" order format.
        if not ticker.replace(".", "").replace("-", "").isalnum():
            print(f"Skipping malformed ticker: {ticker!r}", file=sys.stderr)
            continue
        # Parse score from 3rd field: "+5.23%" → 5.23
        score = abs(float(data[2].rstrip('%'))) if len(data) > 2 else 2.0
        # Scale allocation: stronger signals get more capital (up to 25%, min 10%)
        alloc_pct = min(0.25, max(0.10, score / 100.0))
        try:
            proc = subprocess.run(
                ["python3", f"{lhome}/scripts/price_feed.py", ticker],
                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                text=True, timeout=10,
            )
            result = proc.stdout.split()
            price = float(result[-1]) if result else 100.0
        except Exception:
            price = 100.0
        pos_size = int((capital * alloc_pct) / price)
        if pos_size < 1: continue
        order = f"{ticker} {signal} {pos_size} {price:.2f}"
        orders.append(order)
        print(f"ORDER: {order}")

os.makedirs(logdir, exist_ok=True)
with open(f"{logdir}/momentum_orders.txt", "w") as f:
    f.write("\n".join(orders) + "\n")

sys.exit(0 if orders else 1)