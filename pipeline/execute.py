#!/usr/bin/env python3
"""
QShell Pipeline Stage 5 — execute.py
======================================
SINK stage: sends sized orders to broker. Terminal stage — writes nothing
to stdout (or writes execution receipts as JSON for audit chaining).

Reads  : JSON array of sized candidates from stdin
Writes : JSON array of execution receipts to stdout (for |> audit logging)
Exit   : 0 = all orders sent, 1 = any order failed

Usage:
    ... | python3 size_positions.py | python3 execute.py
    ... | python3 execute.py --mode paper
    ... | python3 execute.py --mode live --slippage_bps 2
    ... | python3 execute.py --mode paper |> logs/executions.csv
"""
import sys
import json
import os
import random
import hashlib
from datetime import datetime

def execute_order(candidate, mode="paper", slippage_bps=2):
    """Simulate or send an order. Returns execution receipt dict."""
    symbol  = candidate["symbol"]
    side    = candidate["side"]
    size    = candidate["size"]
    price   = candidate["price"]

    # Slippage model
    slip = price * slippage_bps / 10_000
    fill_price = round(price + slip if side == "BUY" else price - slip, 2)
    notional   = round(fill_price * size, 2)

    receipt = {
        "symbol":          symbol,
        "side":            side,
        "size":            size,
        "requested_price": price,
        "fill_price":      fill_price,
        "notional":        notional,
        "slippage_bps":    slippage_bps,
        "mode":            mode,
        "status":          "FILLED",
        "timestamp":       datetime.now().isoformat(),
        "meta":            candidate.get("meta", {}),
    }
    return receipt

def main():
    args         = sys.argv[1:]
    mode         = os.environ.get("ACCOUNT", "paper").lower()
    slippage_bps = 2
    dry_run      = False

    i = 0
    while i < len(args):
        if args[i] == "--mode" and i+1 < len(args):
            mode = args[i+1].lower(); i += 2
        elif args[i] == "--slippage_bps" and i+1 < len(args):
            slippage_bps = int(args[i+1]); i += 2
        elif args[i] == "--dry_run":
            dry_run = True; i += 1
        else:
            i += 1

    # ── Safety gate: refuse live mode without explicit flag ───────
    if mode == "live" and not os.environ.get("QSHELL_LIVE_CONFIRMED"):
        print("[execute] LIVE mode requires QSHELL_LIVE_CONFIRMED=1 env var", file=sys.stderr)
        sys.exit(1)

    # ── Read candidates ──────────────────────────────────────────
    raw = sys.stdin.read().strip()
    if not raw or raw == "[]":
        print("[]")
        print("[execute] no orders to execute", file=sys.stderr)
        sys.exit(0)

    try:
        candidates = json.loads(raw)
    except json.JSONDecodeError as e:
        print(f"[execute] invalid JSON: {e}", file=sys.stderr)
        sys.exit(1)

    # ── Validate all candidates have size set ────────────────────
    unsized = [c["symbol"] for c in candidates if c.get("size", 0) == 0]
    if unsized:
        print(f"[execute] unsized candidates: {unsized} — run size_positions first", file=sys.stderr)
        sys.exit(1)

    # ── Execute orders ───────────────────────────────────────────
    receipts  = []
    all_ok    = True

    for c in candidates:
        if dry_run:
            print(
                f"[execute] DRY_RUN {c['side']} {c['size']} {c['symbol']} "
                f"@ ${c['price']:.2f}  notional=${c['size']*c['price']:,.0f}",
                file=sys.stderr
            )
            receipts.append({**c, "status": "DRY_RUN", "timestamp": datetime.now().isoformat()})
            continue

        try:
            receipt = execute_order(c, mode, slippage_bps)
            receipts.append(receipt)
            print(
                f"[execute] FILLED {receipt['side']} {receipt['size']} "
                f"{receipt['symbol']} @ ${receipt['fill_price']:.2f}  "
                f"notional=${receipt['notional']:,.0f}  mode={mode}",
                file=sys.stderr
            )
        except Exception as e:
            print(f"[execute] ERROR on {c['symbol']}: {e}", file=sys.stderr)
            all_ok = False

    # ── Write receipts to stdout for audit chaining ──────────────
    print(json.dumps(receipts, indent=2))

    total_notional = sum(r.get("notional", 0) for r in receipts)
    print(
        f"[execute] {len(receipts)} orders {mode.upper()} | "
        f"total_notional=${total_notional:,.0f}",
        file=sys.stderr
    )

    sys.exit(0 if all_ok else 1)

if __name__ == "__main__":
    main()