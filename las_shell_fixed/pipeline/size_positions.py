#!/usr/bin/env python3
"""
Las_shell Pipeline Stage 4 — size_positions.py
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

def load_risk_config():
    """Same ~/.las_shell_risk 'KEY = VALUE' format risk_config.c and the
    other pipeline/script risk checks read.

    FIX CR5/PR2: risk_filter's size/notional checks were gated behind
    'if size > 0', but risk_filter runs BEFORE size_positions in every
    documented pipeline configuration (see pipeline/run_pipeline.sh) --
    every candidate reaching risk_filter has size == 0 by construction,
    so that guard was never true and the check was structurally dead
    code. Rather than reorder the documented 5-stage pipeline or fake a
    notional estimate with no real size, enforce the actual limits HERE,
    right after size is computed from real data, which is the first
    point in the pipeline where a size/notional check can mean anything."""
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

def main():
    args     = sys.argv[1:]
    capital  = float(os.environ.get("CAPITAL", 100_000))
    model    = "equal"
    amount   = None     # for fixed model
    max_pct  = 0.20     # max 20% per position

    file_cfg  = load_risk_config()
    max_size     = int(float(file_cfg["MAX_POSITION_SIZE"])) if "MAX_POSITION_SIZE" in file_cfg else None
    max_notional = float(file_cfg["MAX_ORDER_NOTIONAL"]) if "MAX_ORDER_NOTIONAL" in file_cfg else None
    blacklist    = set()
    if "BLOCKED_SYMBOLS" in file_cfg:
        blacklist = {s.strip().upper() for s in file_cfg["BLOCKED_SYMBOLS"].split(",") if s.strip()}

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
            # FIX PS3: b came from unvalidated candidate meta with no
            # guard. b <= 0 (e.g. a malformed or malicious meta value)
            # made '(p*b-q)/b' a ZeroDivisionError that crashed the
            # entire run for every candidate, not just this one. Skip
            # just the offending candidate instead.
            if b <= 0:
                print(f"[size_positions] {c.get('symbol','?')}: "
                      f"invalid avg_win_loss_ratio {b} (must be > 0), "
                      f"skipping (weight=0)", file=sys.stderr)
                weights.append(0.0)
                continue
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

    # FIX PS1: cap total deployed capital across ALL candidates, not just
    # each individual position. Without this, e.g. the "fixed" model
    # assigns the same max_pct weight to every candidate with no division
    # by n -- 10 candidates at 20% each deploys 200% of capital. Scale the
    # whole vector down proportionally if it would exceed 100%.
    total_weight = sum(weights)
    if total_weight > 1.0:
        print(f"[size_positions] total allocation {total_weight*100:.1f}% exceeds "
              f"100% of capital -- scaling all positions down proportionally",
              file=sys.stderr)
        weights = [w / total_weight for w in weights]

    # ── Set size on each candidate ────────────────────────────────
    for c, w in zip(candidates, weights):
        price = float(c.get("price", 0))
        if price <= 0:
            # FIX PS2: previously defaulted to $1.00, which silently
            # produces a wildly oversized share count for any candidate
            # missing a real price. Fail closed: size 0, and record why.
            print(f"[size_positions] {c.get('symbol','?')}: no usable price, "
                  f"skipping (size=0)", file=sys.stderr)
            c["size"] = 0
            c["price"] = price
            c.setdefault("meta", {}).update({
                "stage": "size_positions", "model": model,
                "skip_reason": "no_price",
            })
            continue
        alloc    = capital * w
        size     = max(1, int(alloc / price))

        # FIX CR5/PR2: enforce the limits risk_filter could never check
        # (size didn't exist yet there). Fail closed, same style as the
        # PS2 no-price fix above: reject rather than silently clip.
        symbol = c.get("symbol", "?").upper()
        skip_reason = None
        if symbol in blacklist:
            skip_reason = "blacklisted"
        elif max_size is not None and size > max_size:
            skip_reason = f"size {size} > max {max_size}"
        elif max_notional is not None and size * price > max_notional:
            skip_reason = f"notional ${size*price:,.2f} > max ${max_notional:,.2f}"

        if skip_reason:
            print(f"[size_positions] {symbol}: {skip_reason}, skipping (size=0)",
                  file=sys.stderr)
            c["size"] = 0
            c["price"] = price
            c.setdefault("meta", {}).update({
                "stage": "size_positions", "model": model,
                "skip_reason": skip_reason,
            })
            continue

        c["size"] = size
        c.setdefault("meta", {}).update({
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