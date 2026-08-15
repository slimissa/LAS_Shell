#!/usr/bin/env python3
"""
Las_shell P&L Reporter — pnl_report.py
Reads execution log and computes realized/unrealized P&L.
Usage: python3 pnl_report.py [--log FILE] [--format text|json]

⚠ SYNTHETIC DATA — TESTING ONLY: when no real execution log is present,
sim_pnl() fabricates P&L from a date-seeded random distribution, not
real trading results. Even when a log is present, load_executions()
only stores the raw log line rather than parsed financial data (see
PN1/PN2). Useful for exercising the reporting pipeline, not for
evaluating real performance. Do not use for production accounting.
"""
import sys
import os
import json
import random
import hashlib
from datetime import date, datetime

def load_executions(log_file):
    if not os.path.exists(log_file):
        return []
    executions = []
    with open(log_file) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            # Parse: "TIMESTAMP,FILLED TICKER ACTION SIZE @ PRICE notional=N"
            parts = line.split(",", 1)
            if len(parts) < 2:
                continue
            ts, order = parts
            executions.append({"timestamp": ts, "raw": order})
    return executions

def sim_pnl():
    """Simulate P&L when no execution log exists."""
    seed = int(hashlib.md5(f"{date.today()}pnl".encode()).hexdigest(), 16) % 9999
    random.seed(seed)
    realized   = round(random.gauss(250, 800), 2)
    unrealized = round(random.gauss(100, 400), 2)
    total      = round(realized + unrealized, 2)
    trades     = random.randint(3, 15)
    win_rate   = round(random.uniform(0.45, 0.65), 2)
    return realized, unrealized, total, trades, win_rate

def main():
    args = sys.argv[1:]
    log_file = os.path.join(os.environ.get("LAS_SHELL_HOME", "."), "logs", "executions.csv")
    fmt = "text"
    i = 0
    while i < len(args):
        if args[i] == "--log" and i+1 < len(args):
            log_file = args[i+1]; i += 2
        elif args[i] == "--format" and i+1 < len(args):
            fmt = args[i+1]; i += 2
        else:
            i += 1

    executions = load_executions(log_file)
    realized, unrealized, total, trades, win_rate = sim_pnl()

    # FIX PN3: this used to write to ~/.las_shell_pnl -- the SAME file
    # broker.c writes real, fill-derived P&L into after every order (see
    # write_pnl_file() in broker.c), and the one the interactive prompt
    # (prompt.c) reads for live display. This script's own number is not
    # trustworthy in the same way: sim_pnl() is synthetic/random, and even
    # when a real execution log is present, load_executions() only stores
    # the raw log line -- it doesn't parse out actual size/price/notional
    # (see PN1/PN2, a separate issue). Writing that into broker.c's file
    # meant running the 'pnl' alias could silently replace the trader's
    # real live P&L in their own prompt with a fabricated number. Use a
    # distinct file for this script's own report so the two never collide;
    # ~/.las_shell_pnl stays exclusively broker.c's.
    home = os.environ.get("HOME", ".")
    pnl_file = os.path.join(home, ".las_shell_pnl_report")
    with open(pnl_file, "w") as f:
        f.write(f"{total}\n")

    if fmt == "json":
        print(json.dumps({
            "date": str(date.today()),
            "realized_pnl": realized,
            "unrealized_pnl": unrealized,
            "total_pnl": total,
            "trades": trades,
            "win_rate": win_rate,
            "executions": len(executions)
        }, indent=2))
    else:
        sign = "+" if total >= 0 else ""
        print(f"── P&L Report {date.today()} ──────────────────")
        print(f"  Realized   : ${realized:+.2f}")
        print(f"  Unrealized : ${unrealized:+.2f}")
        print(f"  Total      : ${total:+.2f}")
        print(f"  Trades     : {trades}  Win Rate: {win_rate:.0%}")
        print(f"────────────────────────────────────────────")

if __name__ == "__main__":
    main()