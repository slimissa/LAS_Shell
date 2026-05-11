#!/usr/bin/env python3
"""Generate pairs trading orders from spread signal."""
import os, sys

lhome    = os.environ.get("LAS_SHELL_HOME", ".")
logdir   = os.environ.get("LOGDIR", "logs")
pair_a   = os.environ.get("PAIR_A", "AAPL")
pair_b   = os.environ.get("PAIR_B", "MSFT")
pos_size = int(os.environ.get("POS_SIZE", 500))
entry_z  = float(os.environ.get("ENTRY_Z", 2.0))
exit_z   = float(os.environ.get("EXIT_Z", 0.5))

result = os.popen(
    f"python3 {lhome}/scripts/pairs_spread.py {pair_a} {pair_b} --lookback 60 2>/dev/null"
).read().strip()

if not result:
    print("No spread data", file=sys.stderr)
    sys.exit(1)

parts  = dict(item.split("=",1) for item in result.split() if "=" in item)
signal = parts.get("SIGNAL", "HOLD")
zscore = float(parts.get("ZSCORE", 0))

def get_price(t):
    r = os.popen(f"python3 {lhome}/scripts/price_feed.py {t} 2>/dev/null").read().split()
    return float(r[-1]) if r else 100.0

pa = get_price(pair_a)
pb = get_price(pair_b)

def load_direction():
    state_file = f"/tmp/las_shell_pairs_{pair_a}_{pair_b}.txt"
    try:
        with open(state_file) as f:
            return f.read().strip()
    except:
        return None

def save_direction(direction):
    state_file = f"/tmp/las_shell_pairs_{pair_a}_{pair_b}.txt"
    with open(state_file, "w") as f:
        f.write(direction)


orders = []
if signal == f"LONG_{pair_a}_SHORT_{pair_b}":
    orders = [f"{pair_a} BUY {pos_size} {pa:.2f}", f"{pair_b} SELL {pos_size} {pb:.2f}"]
    save_direction(f"LONG_{pair_a}_SHORT_{pair_b}")
elif signal == f"LONG_{pair_b}_SHORT_{pair_a}":
    orders = [f"{pair_b} BUY {pos_size} {pb:.2f}", f"{pair_a} SELL {pos_size} {pa:.2f}"]
    save_direction(f"LONG_{pair_b}_SHORT_{pair_a}")
elif signal == "EXIT":
    direction = load_direction()
    if direction == f"LONG_{pair_b}_SHORT_{pair_a}":
        orders = [f"{pair_a} BUY {pos_size} {pa:.2f}", f"{pair_b} SELL {pos_size} {pb:.2f}"]
    else:
        orders = [f"{pair_a} SELL {pos_size} {pa:.2f}", f"{pair_b} BUY {pos_size} {pb:.2f}"]

if not orders:
    print(f"Signal: HOLD (z={zscore:.2f}) — no trade")
    sys.exit(0)

print(f"Signal: {signal} (z={zscore:.2f})")
for o in orders:
    print(f"  ORDER: {o}")

os.makedirs(logdir, exist_ok=True)
with open(f"{logdir}/pairs_orders.txt", "w") as f:
    f.write("\n".join(o.strip() for o in orders) + "\n")
sys.exit(0)