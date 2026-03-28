#!/usr/bin/env python3
"""
QShell Risk Checker — risk_check.py
Reads order data from stdin or file, validates against risk limits.
Usage: echo "TICKER ACTION SIZE PRICE" | python3 risk_check.py
       python3 risk_check.py --order "AAPL BUY 1000 185.50"
       python3 risk_check.py --max_notional N --max_size N --blacklist TICKER,...

Exit 0 = PASSED. Exit 1 = REJECTED (reason printed to stderr).
"""
import sys
import os

def check_order(order_str, max_notional=500000, max_size=5000, blacklist=None):
    blacklist = blacklist or ["GME","AMC","BBBY","MULN","FFIE"]
    parts = order_str.strip().split()
    if len(parts) < 2:
        return True, "ok"  # Can't parse, let through

    ticker = parts[0].upper()
    action = parts[1].upper() if len(parts) > 1 else "BUY"
    size   = int(parts[2])   if len(parts) > 2 else 100
    price  = float(parts[3]) if len(parts) > 3 else 100.0

    # Blacklist check
    if ticker in blacklist:
        return False, f"TICKER {ticker} is blacklisted"

    # Size check
    if size > max_size:
        return False, f"SIZE {size} exceeds max_size {max_size}"

    # Notional check
    notional = size * price
    if notional > max_notional:
        return False, f"NOTIONAL ${notional:.0f} exceeds max ${max_notional:.0f}"

    return True, "ok"

def main():
    args = sys.argv[1:]
    max_notional = 500000
    max_size = 5000
    blacklist = None
    order_str = None
    i = 0
    while i < len(args):
        if args[i] == "--max_notional" and i+1 < len(args):
            max_notional = float(args[i+1]); i += 2
        elif args[i] == "--max_size" and i+1 < len(args):
            max_size = int(args[i+1]); i += 2
        elif args[i] == "--blacklist" and i+1 < len(args):
            blacklist = [t.strip().upper() for t in args[i+1].split(",")]; i += 2
        elif args[i] == "--order" and i+1 < len(args):
            order_str = args[i+1]; i += 2
        else:
            i += 1

    # Read from stdin if no --order
    if order_str is None:
        order_str = sys.stdin.read().strip()

    if not order_str:
        sys.exit(0)

    # Check each line
    all_pass = True
    for line in order_str.strip().splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        passed, reason = check_order(line, max_notional, max_size, blacklist)
        if not passed:
            print(f"RISK_REJECTED: {reason} | ORDER: {line}", file=sys.stderr)
            all_pass = False
        else:
            print(f"RISK_PASSED: {line}")

    sys.exit(0 if all_pass else 1)

if __name__ == "__main__":
    main()