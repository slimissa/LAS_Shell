#!/usr/bin/env python3
"""
QShell Pipeline Stage 3 — risk_filter.py
==========================================
FILTER stage: enforces pre-trade risk rules.
Works both as a pipe stage AND as a ?> risk gate checker.

As pipe stage:  filters candidates, logs rejections to stderr
As ?> checker:  exits 0 if ALL pass, exits 1 if ANY rejected

Reads  : JSON array from stdin
Writes : filtered JSON array (only passing candidates) to stdout
Exit   : 0 = all passed (or filtered cleanly), 1 = hard rejection

Usage:
    python3 universe.py | python3 momentum_filter.py | python3 risk_filter.py
    echo '[{"symbol":"GME",...}]' ?> python3 risk_filter.py   # QShell gate
    python3 risk_filter.py --max_notional 500000 --max_size 5000
"""
import sys
import json
import os

# ── Default limits ────────────────────────────────────────────────
DEFAULT_MAX_NOTIONAL  = 500_000.0
DEFAULT_MAX_SIZE      = 5_000
DEFAULT_MIN_SIGNAL    = 0.1
DEFAULT_BLACKLIST     = {"GME", "AMC", "BBBY", "MULN", "FFIE", "SPCE"}

def check_candidate(c, max_notional, max_size, min_signal, blacklist):
    """Returns (passed: bool, reason: str)."""
    symbol  = c.get("symbol", "").upper()
    signal  = float(c.get("signal", 0))
    size    = int(c.get("size", 0))
    price   = float(c.get("price", 0))

    if symbol in blacklist:
        return False, f"{symbol} is blacklisted"

    if abs(signal) < min_signal:
        return False, f"{symbol} signal {signal:.3f} below min {min_signal}"

    # Size-based checks only if size is already set
    if size > 0:
        if size > max_size:
            return False, f"{symbol} size {size} > max {max_size}"
        notional = size * price
        if notional > max_notional:
            return False, f"{symbol} notional ${notional:,.0f} > max ${max_notional:,.0f}"

    return True, "ok"

def main():
    args         = sys.argv[1:]
    max_notional = DEFAULT_MAX_NOTIONAL
    max_size     = DEFAULT_MAX_SIZE
    min_signal   = DEFAULT_MIN_SIGNAL
    blacklist    = set(DEFAULT_BLACKLIST)
    gate_mode    = False   # True when used as ?> checker

    i = 0
    while i < len(args):
        if args[i] == "--max_notional" and i+1 < len(args):
            max_notional = float(args[i+1]); i += 2
        elif args[i] == "--max_size" and i+1 < len(args):
            max_size = int(args[i+1]); i += 2
        elif args[i] == "--min_signal" and i+1 < len(args):
            min_signal = float(args[i+1]); i += 2
        elif args[i] == "--blacklist" and i+1 < len(args):
            blacklist |= {s.strip().upper() for s in args[i+1].split(",")}; i += 2
        elif args[i] == "--gate":
            gate_mode = True; i += 1
        else:
            i += 1

    # ── Read from stdin ──────────────────────────────────────────
    raw = sys.stdin.read().strip()
    if not raw or raw == "[]":
        print("[]"); sys.exit(0)

    try:
        candidates = json.loads(raw)
    except json.JSONDecodeError as e:
        print(f"[risk_filter] invalid JSON: {e}", file=sys.stderr)
        sys.exit(1)

    # ── Apply risk rules ─────────────────────────────────────────
    passed_list  = []
    rejected_any = False

    for c in candidates:
        ok, reason = check_candidate(c, max_notional, max_size, min_signal, blacklist)
        if ok:
            c["meta"].update({"stage": "risk_filter", "risk_status": "PASSED"})
            passed_list.append(c)
        else:
            rejected_any = True
            print(f"[risk_filter] REJECTED {c.get('symbol')}: {reason}", file=sys.stderr)
            # Log to rejection file
            home = os.environ.get("HOME", ".")
            with open(f"{home}/.qshell_risk_rejections", "a") as f:
                from datetime import datetime
                f.write(f"{datetime.now().isoformat()},REJECTED,{c.get('symbol')},{reason}\n")

    print(json.dumps(passed_list, indent=2))
    print(
        f"[risk_filter] {len(candidates)} in → {len(passed_list)} passed, "
        f"{len(candidates)-len(passed_list)} rejected",
        file=sys.stderr
    )

    # In gate mode: exit 1 if ANY rejection
    if gate_mode and rejected_any:
        sys.exit(1)

    sys.exit(0)

if __name__ == "__main__":
    main()