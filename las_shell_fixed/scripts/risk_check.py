#!/usr/bin/env python3
"""
Las_shell Risk Checker — risk_check.py
Reads order data from stdin or file, validates against risk limits.
Usage: echo "TICKER ACTION SIZE PRICE" | python3 risk_check.py
       python3 risk_check.py --order "AAPL BUY 1000 185.50"
       python3 risk_check.py --max_notional N --max_size N --blacklist TICKER,...

Exit 0 = PASSED. Exit 1 = REJECTED (reason printed to stderr).
"""
import sys
import os

def load_risk_config():
    """Parse ~/.las_shell_risk (same 'KEY = VALUE' format risk_config.c's
    load_risk_config() reads -- '#' comments, blank lines ignored, unknown
    keys silently ignored) so this, the actual ?> gate checker real
    strategies run through, enforces the SAME limits as the C-side risk
    gate instead of a hardcoded, independent set."""
    home = os.environ.get("HOME", ".")
    cfg = {}
    try:
        with open(os.path.join(home, ".las_shell_risk")) as f:
            for line in f:
                line = line.split("#", 1)[0].strip()
                if not line or "=" not in line:
                    continue
                key, val = line.split("=", 1)
                cfg[key.strip().upper()] = val.strip()
    except OSError:
        pass  # no config file -- hardcoded defaults stand
    return cfg

def check_order(order_str, max_notional=500000, max_size=5000, blacklist=None):
    blacklist = blacklist or ["GME","AMC","BBBY","MULN","FFIE"]
    parts = order_str.strip().split()

    # FIX RK2: a line this checker can't parse into at least
    # TICKER ACTION SIZE PRICE used to pass through unconditionally
    # ("Can't parse, let through"). An order the risk gate can't actually
    # validate should be rejected, not waved through.
    if len(parts) < 4:
        return False, f"cannot validate — expected 'TICKER ACTION SIZE PRICE', got {len(parts)} field(s)"

    ticker = parts[0].upper()
    action = parts[1].upper()

    # FIX RK3/RK4: size/price used to default to 100/$100.0 when missing,
    # so a malformed or market order with no real price got "validated"
    # against a fabricated notional instead of being rejected. With the
    # len(parts) < 4 check above, both fields are always present in the
    # string by this point -- but still guard the actual parse, since a
    # non-numeric field (e.g. "AAPL BUY MANY LOTS") would otherwise raise
    # an uncaught exception instead of a clean rejection.
    try:
        size  = int(parts[2])
        price = float(parts[3])
    except ValueError:
        return False, f"cannot validate — non-numeric size/price in '{parts[2]} {parts[3]}'"

    if price <= 0:
        return False, f"cannot validate notional for {ticker} — no usable price ({price})"

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
    file_cfg = load_risk_config()
    max_notional = float(file_cfg["MAX_ORDER_NOTIONAL"]) if "MAX_ORDER_NOTIONAL" in file_cfg else 500000
    max_size     = int(float(file_cfg["MAX_POSITION_SIZE"])) if "MAX_POSITION_SIZE" in file_cfg else 5000
    blacklist    = None
    if "BLOCKED_SYMBOLS" in file_cfg:
        blacklist = [s.strip().upper() for s in file_cfg["BLOCKED_SYMBOLS"].split(",") if s.strip()]
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