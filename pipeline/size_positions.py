#!/usr/bin/env python3
"""
QShell Pipeline Stage 4 — size_positions.py
=============================================
ENRICH stage: computes position sizes based on capital allocation model.
Sets the `size` field on every candidate. Never filters.

Allocation models:
  equal       : equal weight across all candidates (default)
  signal      : weight proportional to |signal|
  kelly       : fractional Kelly (requires win_rate and avg_win/loss in meta)
  fixed       : fixed dollar amount per position

Reads  : JSON array from stdin
Writes : same JSON array with `size` field populated to stdout

Usage:
    ... | python3 size_positions.py
    ... | python3 size_positions.py --model signal --capital 250000
    ... | python3 size_positions.py --model fixed --amount 10000
    ... | python3 size_positions.py --max_position_pct 0.10
"""
import sys
import json
import os

def main():
    args     = sys.argv[1:]
    capital  = float(os.environ.get("CAPITAL", 100_000))
    model    = "equal"
    amount   = None     # for fixed model
    max_pct  = 0.20     # max 20% per position

    i = 0
    while i < len(args):
        if args[i] == "--capital" and i+1 < len(args):
            capital = float(args[i+1]); i += 2
        elif args[i] == "--model" and i+1 < len(args):
            model = args[i+1]; i += 2
        elif args[i] == "--amount" and i+1 < len(args):
            amount = float(args[i+1]); i += 2
        elif args[i] == "--max_position_pct" and i+1 < len(args):
            max_pct = float(args[i+1]); i += 2
        else:
            i += 1

    # ── Read candidates ──────────────────────────────────────────
    raw = sys.stdin.read().strip()
    if not raw or raw == "[]":
        print("[]"); sys.exit(0)

    try:
        candidates = json.loads(raw)
    except json.JSONDecodeError as e:
        print(f"[size_positions] invalid JSON: {e}", file=sys.stderr)
        sys.exit(1)

    if not candidates:
        print("[]"); sys.exit(0)

    n = len(candidates)

    # ── Compute allocation weights ───────────────────────────────
    if model == "equal":
        weights = [1.0 / n] * n

    elif model == "signal":
        abs_signals = [abs(float(c.get("signal", 0))) for c in candidates]
        total = sum(abs_signals) or 1.0
        weights = [s / total for s in abs_signals]

    elif model == "kelly":
        # Fractional Kelly: f = (p*b - q) / b
        # Uses meta.win_rate and meta.avg_win_loss_ratio if available
        weights = []
        for c in candidates:
            meta    = c.get("meta", {})
            p       = float(meta.get("win_rate", 0.55))
            b       = float(meta.get("avg_win_loss_ratio", 1.5))
            q       = 1.0 - p
            kelly   = max(0.0, (p * b - q) / b)
            frac    = kelly * 0.25   # quarter-Kelly for safety
            weights.append(min(frac, max_pct))
        total = sum(weights) or 1.0
        weights = [w / total for w in weights]

    elif model == "fixed":
        dollar = amount or (capital * max_pct)
        weights = [dollar / capital] * n

    else:
        print(f"[size_positions] unknown model: {model}", file=sys.stderr)
        sys.exit(1)

    # ── Apply max position cap ────────────────────────────────────
    weights = [min(w, max_pct) for w in weights]

    # ── Set size on each candidate ────────────────────────────────
    for c, w in zip(candidates, weights):
        price    = float(c.get("price", 1.0))
        alloc    = capital * w
        size     = max(1, int(alloc / price))

        c["size"] = size
        c["meta"].update({
            "stage":         "size_positions",
            "model":         model,
            "capital":       capital,
            "allocation":    round(alloc, 2),
            "weight":        round(w, 4),
        })

    print(json.dumps(candidates, indent=2))
    total_alloc = sum(c["size"] * c["price"] for c in candidates)
    print(
        f"[size_positions] {n} positions sized | model={model} | "
        f"capital=${capital:,.0f} | total_notional=${total_alloc:,.0f}",
        file=sys.stderr
    )
    sys.exit(0)

if __name__ == "__main__":
    main()