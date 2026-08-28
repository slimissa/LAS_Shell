#!/usr/bin/env python3
"""
Las_shell P&L Reporter — pnl_report.py
Reads execution log and computes realized/unrealized P&L.
Usage: python3 pnl_report.py [--log FILE] [--format text|json]

⚠ SYNTHETIC DATA — TESTING ONLY (fallback path): sim_pnl() fabricates
P&L from a date-seeded random distribution and is used ONLY when no
real paper-account ledger can be read (see get_real_pnl() below). When
a real ledger exists, this script now reports genuinely real numbers.

Milestone 3 Phase 7 — multi-currency P&L
-----------------------------------------
Real per-position P&L (avg_cost, realized_pnl, currency) already lives
in broker.c's paper-account ledger and is exposed correctly, including
current market price and unrealized P&L, via `las_shell -c "positions
--json"`. Re-deriving P&L independently here from the raw order-log
CSV would mean re-implementing broker.c's own avg-cost/FIFO accounting
and live-price lookup a second time in Python -- guaranteed to drift
from the real numbers eventually. Instead this script shells out to
the real binary and aggregates its (now currency-tagged) output by
currency, the same grouping/safety rule broker.c's own
paper_print_positions() uses:
  - Only the BASE_CURRENCY group is summed into the headline total.
  - Any other-currency group is reported separately, UNCONVERTED --
    there is no FX rate source anywhere in this codebase, so a
    conversion would have to fabricate a rate. Not doing that.
  - In today's system every position is USD (or BASE_CURRENCY, if
    configured) -- see broker.c's Position struct comment for why.
    This code is real and general regardless: it will start showing
    a genuine multi-currency breakdown the moment a non-base-currency
    position exists, no changes needed here.

If `las_shell -c "positions --json"` can't be run (binary not found,
no paper account yet, etc.), this falls back to the pre-existing
sim_pnl() synthetic path, unchanged and still clearly labeled -- the
fallback is honest about being fake; the primary path is not.
"""
import sys
import os
import json
import random
import hashlib
import subprocess
import shutil
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

def find_las_shell_binary():
    """Locate the las_shell binary: $LAS_SHELL_HOME/las_shell first
    (matches how every other script in this repo resolves it), then
    PATH, mirroring how a `make install`-ed system would have it."""
    home = os.environ.get("LAS_SHELL_HOME")
    if home:
        candidate = os.path.join(home, "las_shell")
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return shutil.which("las_shell")

def get_base_currency():
    """Mirrors risk_config_base_currency()'s default: BASE_CURRENCY
    from ~/.las_shell_risk if set and non-empty, else "USD". Read
    directly rather than shelling out again -- this is a plain text
    file, not worth a second subprocess call."""
    home = os.environ.get("HOME", ".")
    path = os.path.join(home, ".las_shell_risk")
    if os.path.exists(path):
        try:
            with open(path) as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("#") or "=" not in line:
                        continue
                    key, val = line.split("=", 1)
                    if key.strip().upper() == "BASE_CURRENCY":
                        v = val.strip()
                        if v:
                            return v.upper()
        except OSError:
            pass
    return "USD"

def get_real_pnl():
    """Returns (by_currency, base_currency) on success, None if a real
    ledger can't be read. by_currency is {code: {"unrealized": f,
    "realized": f, "market_value": f}}."""
    binary = find_las_shell_binary()
    if not binary:
        return None
    try:
        result = subprocess.run(
            [binary, "-c", "positions --json"],
            capture_output=True, text=True, timeout=10
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    if result.returncode != 0:
        return None
    try:
        positions = json.loads(result.stdout)
    except json.JSONDecodeError:
        return None
    if not isinstance(positions, list):
        return None

    by_currency = {}
    for p in positions:
        cur = p.get("currency", "USD") or "USD"
        bucket = by_currency.setdefault(
            cur, {"unrealized": 0.0, "realized": 0.0, "market_value": 0.0})
        bucket["unrealized"]   += p.get("unrealized_pnl", 0.0)
        bucket["realized"]    += p.get("realized_pnl", 0.0)
        bucket["market_value"] += p.get("market_value", 0.0)

    return by_currency, get_base_currency()

def main():
    args = sys.argv[1:]
    log_file = os.path.join(os.environ.get("HOME", "."), ".las_shell_order_log")
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
    real = get_real_pnl()

    if real is not None:
        by_currency, base_cur = real
        base = by_currency.get(base_cur, {"unrealized": 0.0, "realized": 0.0, "market_value": 0.0})
        realized   = round(base["realized"], 2)
        unrealized = round(base["unrealized"], 2)
        total      = round(realized + unrealized, 2)
        trades     = len(executions)
        win_rate   = None  # not derivable from the ledger alone -- not fabricated
        is_synthetic = False
        other_currencies = {c: v for c, v in by_currency.items() if c != base_cur}
    else:
        realized, unrealized, total, trades, win_rate = sim_pnl()
        base_cur = get_base_currency()
        is_synthetic = True
        other_currencies = {}

    # FIX PN3: this used to write to ~/.las_shell_pnl -- the SAME file
    # broker.c writes real, fill-derived P&L into after every order (see
    # write_pnl_file() in broker.c), and the one the interactive prompt
    # (prompt.c) reads for live display. Writing this script's own number
    # into that file meant running the 'pnl' alias could silently replace
    # the trader's real live P&L in their own prompt -- true even now that
    # this script's primary path reports real numbers, since a stale
    # report run between trades could still race with a fresher live
    # figure. Keep them in permanently separate files.
    home = os.environ.get("HOME", ".")
    pnl_file = os.path.join(home, ".las_shell_pnl_report")
    with open(pnl_file, "w") as f:
        f.write(f"{total}\n")

    if fmt == "json":
        out = {
            "date": str(date.today()),
            "base_currency": base_cur,
            "realized_pnl": realized,
            "unrealized_pnl": unrealized,
            "total_pnl": total,
            "trades": trades,
            "synthetic": is_synthetic,
        }
        if win_rate is not None:
            out["win_rate"] = win_rate
        if other_currencies:
            out["other_currencies"] = {
                c: {"unrealized_pnl": round(v["unrealized"], 2),
                    "realized_pnl": round(v["realized"], 2),
                    "market_value": round(v["market_value"], 2),
                    "converted": False}
                for c, v in other_currencies.items()
            }
        print(json.dumps(out, indent=2))
    else:
        print(f"── P&L Report {date.today()} ({base_cur}) ──────────────────")
        if is_synthetic:
            print("  ⚠ SYNTHETIC — no readable paper-account ledger; showing "
                  "simulated data, not real results.")
        print(f"  Realized   : {base_cur} {realized:+.2f}")
        print(f"  Unrealized : {base_cur} {unrealized:+.2f}")
        print(f"  Total      : {base_cur} {total:+.2f}")
        if trades or win_rate is not None:
            wr = f"  Win Rate: {win_rate:.0%}" if win_rate is not None else ""
            print(f"  Trades     : {trades}{wr}")
        if other_currencies:
            print(f"  ── Other currencies (unconverted, NOT in total above) ──")
            for c, v in other_currencies.items():
                print(f"    {c}: unrealized {v['unrealized']:+.2f}  "
                      f"realized {v['realized']:+.2f}  "
                      f"mkt_val {v['market_value']:.2f}")
        print(f"────────────────────────────────────────────")


if __name__ == "__main__":
    main()