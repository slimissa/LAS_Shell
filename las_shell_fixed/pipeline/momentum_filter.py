#!/usr/bin/env python3
"""
Las_shell Pipeline Stage 2 — momentum_filter.py
=============================================
FILTER + ENRICH stage: computes momentum signal for each candidate,
keeps only those above the threshold, sets signal and side fields.

⚠ SYNTHETIC DATA — TESTING ONLY: momentum signal is drawn from an IID
Gaussian, not real price history, so it has no true autocorrelation/
momentum structure (same caveat as scripts/momentum.py). Useful for
exercising the pipeline, not for evaluating strategy performance.

Reads  : JSON array of candidates from stdin
Writes : filtered JSON array with signal field populated to stdout

Usage:
    python3 universe.py | python3 momentum_filter.py
    python3 universe.py | python3 momentum_filter.py --threshold 0.3
    python3 universe.py | python3 momentum_filter.py --lookback 20 --topn 5
"""
import sys
import json
import random
import hashlib
from datetime import date

def compute_momentum(symbol, lookback=20):
    """Simulate N-day momentum return."""
    seed = int(hashlib.md5(
        f"{symbol}{date.today()}momentum{lookback}".encode()
    ).hexdigest(), 16) % 9999
    random.seed(seed)
    returns = [random.gauss(0.0003, 0.015) for _ in range(lookback)]
    cum = 1.0
    for r in returns:
        cum *= (1 + r)
    return round(cum - 1.0, 4)  # raw return as signal proxy

def normalize_signal(raw_return, clip=0.15):
    """Map raw return to [-1, 1] signal by clipping at +-clip.

    FIX PM2: the 0.15 (15%) default was previously an unexplained magic
    number. It's a normalization bound, not a trading threshold: raw
    lookback-period returns get mapped onto a [-1, 1] signal scale by
    treating anything at or beyond +-15% as "maximally strong" (signal
    +-1.0), so a downstream consumer doesn't need to know the raw return
    scale. There's no data-driven derivation behind 15% specifically --
    it's a reasonable-looking round number for a lookback-period equity
    return. Pass a different `clip` if your lookback window or asset
    class warrants a different normalization range.
    """
    clamped = max(-clip, min(clip, raw_return))
    return round(clamped / clip, 4)

def main():
    args       = sys.argv[1:]
    threshold  = 0.2     # minimum |signal| to pass
    lookback   = 20
    topn       = None
    i = 0
    while i < len(args):
        if args[i] == "--threshold" and i+1 < len(args):
            threshold = float(args[i+1]); i += 2
        elif args[i] == "--lookback" and i+1 < len(args):
            lookback = int(args[i+1]); i += 2
        elif args[i] == "--topn" and i+1 < len(args):
            topn = int(args[i+1]); i += 2
        else:
            i += 1

    # ── Read candidates from stdin ──────────────────────────────
    raw = sys.stdin.read().strip()
    if not raw or raw == "[]":
        print("[]"); sys.exit(0)

    try:
        candidates = json.loads(raw)
    except json.JSONDecodeError as e:
        print(f"[momentum_filter] invalid JSON: {e}", file=sys.stderr)
        sys.exit(1)

    # ── Compute and apply momentum signal ───────────────────────
    enriched = []
    for c in candidates:
        symbol     = c.get("symbol", "")
        raw_ret    = compute_momentum(symbol, lookback)
        signal     = normalize_signal(raw_ret)
        if signal > 0.0:       side = "BUY"
        elif signal < 0.0:     side = "SELL"
        else:                  side = "HOLD"
        
        c["signal"] = signal
        c["side"]   = side
        c.setdefault("meta", {}).update({
            "stage":         "momentum_filter",
            "raw_return":    raw_ret,
            "lookback_days": lookback,
        })
        enriched.append((signal, c))

    # ── Filter by threshold ─────────────────────────────────────
    passed = [(sig, c) for sig, c in enriched if abs(sig) >= threshold]

    # ── Rank by absolute signal, optionally take topN ───────────
    passed.sort(key=lambda x: abs(x[0]), reverse=True)
    if topn:
        passed = passed[:topn]

    result = [c for _, c in passed]

    print(json.dumps(result, indent=2))
    print(
        f"[momentum_filter] {len(candidates)} in → {len(result)} passed "
        f"(threshold={threshold}, lookback={lookback})",
        file=sys.stderr
    )
    sys.exit(0)

if __name__ == "__main__":
    main()