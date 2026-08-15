#!/usr/bin/env python3
"""
Las_shell Order Executor — order_executor.py
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

# FIX OE2: this executor previously had no risk checks at all -- it
# trusted whatever reached it. These are the same minimum checks
# risk_check.py already performs (blacklist / max size / max notional),
# applied here too as defense-in-depth: an order that skipped the ?>
# gate, or arrived some other way, still gets validated before being
# marked FILLED instead of executing unconditionally.
DEFAULT_MAX_NOTIONAL = 500_000.0
DEFAULT_MAX_SIZE     = 5_000
DEFAULT_BLACKLIST    = {"GME", "AMC", "BBBY", "MULN", "FFIE", "SPCE"}

def load_risk_config():
    """Same ~/.las_shell_risk 'KEY = VALUE' format risk_config.c and the
    PR1-fixed risk_filter.py read."""
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
        pass
    return cfg

def validate_order(ticker, size, price, max_notional, max_size, blacklist):
    """Returns (ok: bool, reason: str)."""
    if ticker in blacklist:
        return False, f"{ticker} is blacklisted"
    if size > max_size:
        return False, f"size {size} exceeds max_size {max_size}"
    notional = size * price
    if notional > max_notional:
        return False, f"notional ${notional:,.2f} exceeds max ${max_notional:,.2f}"
    return True, "ok"

def execute_order(order_str, mode="paper", slippage_bps=2, max_notional=DEFAULT_MAX_NOTIONAL,
                   max_size=DEFAULT_MAX_SIZE, blacklist=None, position_book=None):
    blacklist = blacklist if blacklist is not None else DEFAULT_BLACKLIST
    parts = order_str.strip().split()
    if len(parts) < 3:
        return None
    ticker = parts[0].upper()
    action = parts[1].upper()
    size   = int(parts[2])
    price  = float(parts[3]) if len(parts) > 3 else 100.0
    # FIX OE3/OE4: optional 5th field for order type. Defaults to market
    # for backward compatibility with the existing 4-field order format.
    order_type = parts[4].lower() if len(parts) > 4 else "market"
    if order_type not in ("market", "limit"):
        order_type = "market"

    ok, reason = validate_order(ticker, size, price, max_notional, max_size, blacklist)
    if not ok:
        return {
            "timestamp": datetime.now().isoformat(),
            "ticker": ticker,
            "action": action,
            "size": size,
            "requested_price": price,
            "order_type": order_type,
            "mode": mode,
            "status": "REJECTED",
            "reason": reason,
        }

    if order_type == "limit":
        # FIX OE3: a limit order fills at exactly the requested price --
        # no slippage. (A real broker might not fill at all if the market
        # never reaches the limit; this simulator always fills, same
        # simplification the rest of this codebase's simulators make.)
        fill_price = round(price, 2)
    else:
        # Market order: simulate slippage against the requested price.
        slip = price * slippage_bps / 10000
        if action == "BUY":
            fill_price = round(price + slip, 2)
        else:
            fill_price = round(price - slip, 2)

    notional = round(fill_price * size, 2)

    # FIX OE5: track a running position per ticker across the orders
    # processed in this run, so the output reflects a real position
    # instead of treating every order as an independent, disconnected
    # event. (In-process only -- see note in main() about persistence.)
    position_after = None
    if position_book is not None:
        signed = size if action == "BUY" else -size
        position_book[ticker] = position_book.get(ticker, 0) + signed
        position_after = position_book[ticker]

    return {
        "timestamp": datetime.now().isoformat(),
        "ticker": ticker,
        "action": action,
        "size": size,
        "requested_price": price,
        "order_type": order_type,
        "fill_price": fill_price,
        "notional": notional,
        "position_after": position_after,
        "mode": mode,
        "status": "FILLED"
    }

def main():
    args = sys.argv[1:]
    mode = "paper"
    slippage_bps = 2
    file_cfg = load_risk_config()
    max_notional = float(file_cfg["MAX_ORDER_NOTIONAL"]) if "MAX_ORDER_NOTIONAL" in file_cfg else DEFAULT_MAX_NOTIONAL
    max_size     = int(float(file_cfg["MAX_POSITION_SIZE"])) if "MAX_POSITION_SIZE" in file_cfg else DEFAULT_MAX_SIZE
    blacklist    = set(DEFAULT_BLACKLIST)
    if "BLOCKED_SYMBOLS" in file_cfg:
        blacklist |= {s.strip().upper() for s in file_cfg["BLOCKED_SYMBOLS"].split(",") if s.strip()}
    i = 0
    while i < len(args):
        if args[i] == "--mode" and i+1 < len(args):
            mode = args[i+1]; i += 2
        elif args[i] == "--slippage_bps" and i+1 < len(args):
            slippage_bps = int(args[i+1]); i += 2
        elif args[i] == "--max_notional" and i+1 < len(args):
            max_notional = float(args[i+1]); i += 2
        elif args[i] == "--max_size" and i+1 < len(args):
            max_size = int(args[i+1]); i += 2
        elif args[i] == "--blacklist" and i+1 < len(args):
            blacklist |= {s.strip().upper() for s in args[i+1].split(",")}; i += 2
        else:
            i += 1

    orders_raw = sys.stdin.read().strip()
    if not orders_raw:
        print("No orders received", file=sys.stderr)
        sys.exit(1)

    results = []
    any_rejected = False
    position_book = {}  # FIX OE5: running position per ticker, this run
    for line in orders_raw.splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        result = execute_order(line, mode, slippage_bps, max_notional, max_size, blacklist, position_book)
        if result:
            results.append(result)
            if result["status"] == "REJECTED":
                any_rejected = True
                print(f"REJECTED {result['ticker']} {result['action']} {result['size']}: {result['reason']}", file=sys.stderr)
            else:
                print(f"FILLED {result['ticker']} {result['action']} {result['size']} "
                      f"({result['order_type']}) @ {result['fill_price']} "
                      f"notional={result['notional']} position={result['position_after']}")

    if not results:
        print("No valid orders to execute", file=sys.stderr)
        sys.exit(1)

    if position_book:
        # FIX OE5: this is in-process only -- it reflects positions built
        # up across the orders in THIS run, not a persistent ledger across
        # separate invocations. Real cross-run position tracking lives in
        # broker.c's paper account file; this is meant for visibility
        # within a single pipeline run, not as a replacement for that.
        summary = ", ".join(f"{t}={q}" for t, q in position_book.items())
        print(f"[order_executor] end-of-run position (this run only): {summary}", file=sys.stderr)

    sys.exit(1 if any_rejected else 0)

if __name__ == "__main__":
    main()