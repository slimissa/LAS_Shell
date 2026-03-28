#!/usr/bin/env python3
"""Generate sized orders from momentum signals CSV."""
import os, sys

logdir  = os.environ.get("LOGDIR", "logs")
capital = float(os.environ.get("CAPITAL", 100000))
qhome   = os.environ.get("QSHELL_HOME", ".")
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
        if signal != "BUY": continue
        try:
            result = os.popen(
                f"python3 {qhome}/scripts/price_feed.py {ticker} 2>/dev/null"
            ).read().split()
            price = float(result[-1]) if result else 100.0
        except:
            price = 100.0
        pos_size = int((capital * 0.20) / price)
        if pos_size < 1: continue
        order = f"{ticker} BUY {pos_size} {price:.2f}"
        orders.append(order)
        print(f"ORDER: {order}")

os.makedirs(logdir, exist_ok=True)
with open(f"{logdir}/momentum_orders.txt", "w") as f:
    f.write("\n".join(orders) + "\n")

sys.exit(0 if orders else 1)