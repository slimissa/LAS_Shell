#!/usr/bin/env python3
"""
Las_shell Backtesting Harness
===========================
Replaces execute_orders / pipeline/execute.py when BACKTEST_MODE=1.

Called automatically by pipeline/execute.py when the ~> operator is active.
Can also be called directly for standalone backtests.

Environment variables set by the ~> operator:
    BACKTEST_MODE=1
    BACKTEST_START=YYYY-MM-DD
    BACKTEST_END=YYYY-MM-DD

Additional optional variables:
    CAPITAL         starting capital (default: 100000)
    BACKTEST_SLIP   slippage in bps  (default: 2)
    BACKTEST_COMMISSION  commission per share in USD (default: 0.005)
    LOGDIR          output directory  (default: ./logs)
    LAS_SHELL_HOME     project root      (default: .)

Usage (direct):
    python3 scripts/backtesting_harness.py
    echo '[{"symbol":"AAPL","signal":0.8,"size":100,"price":185.0,"side":"BUY","meta":{...}}]' \
        | python3 scripts/backtesting_harness.py

Usage (via ~> operator in Las_shell):
    python3 pipeline/universe.py | python3 pipeline/momentum_filter.py \
        | python3 pipeline/risk_filter.py | python3 pipeline/size_positions.py \
        | python3 pipeline/execute.py ~> 2020-01-01:2023-12-31
"""

import sys
import os
import json
import random
import hashlib
from datetime import datetime, date, timedelta

# ── Configuration from environment ──────────────────────────────────────
BACKTEST_MODE  = os.environ.get("BACKTEST_MODE", "0") == "1"
START_DATE_STR = os.environ.get("BACKTEST_START", "2020-01-01")
END_DATE_STR   = os.environ.get("BACKTEST_END",   "2023-12-31")
CAPITAL        = float(os.environ.get("CAPITAL", 100_000))
SLIP_BPS       = float(os.environ.get("BACKTEST_SLIP", 2))
COMMISSION     = float(os.environ.get("BACKTEST_COMMISSION", 0.005))  # per share
LOGDIR = os.environ.get("LOGDIR", os.path.join(os.environ.get("HOME", "."), "las_shell_logs"))

# ── Utilities ────────────────────────────────────────────────────────────
def parse_date(s):
    for fmt in ("%Y-%m-%d", "%Y"):
        try:
            return datetime.strptime(s, fmt).date()
        except ValueError:
            pass
    raise ValueError(f"Cannot parse date: {s!r}")


def date_range_days(start: date, end: date) -> int:
    return max(1, (end - start).days)


def sim_price_at_date(symbol: str, base_price: float, target_date: date) -> float:
    """Deterministic daily price simulation using symbol + date as seed."""
    seed_str = f"{symbol}{target_date.isoformat()}"
    seed = int(hashlib.md5(seed_str.encode()).hexdigest(), 16) % 999983
    random.seed(seed)
    # Random walk: ~0.8% daily vol
    noise = random.gauss(0.0, 0.008)
    return round(base_price * (1.0 + noise), 2)


def apply_slippage(price: float, side: str, slip_bps: float) -> float:
    slip = price * slip_bps / 10_000
    return round(price + slip if side == "BUY" else price - slip, 2)


# ── Core simulation ──────────────────────────────────────────────────────
def simulate_order(candidate: dict, target_date: date) -> dict:
    """
    Simulate a single order fill on target_date.
    Returns an execution receipt compatible with pipeline convention v1.0.
    """
    symbol = candidate["symbol"]
    side   = candidate.get("side", "BUY")
    size   = int(candidate.get("size", 0))
    price  = float(candidate.get("price", 100.0))

    # Simulate fill price at the target date
    sim_price = sim_price_at_date(symbol, price, target_date)
    fill_price = apply_slippage(sim_price, side, SLIP_BPS)
    commission = round(size * COMMISSION, 4)
    notional   = round(fill_price * size, 2)
    net_cost   = round(notional + commission if side == "BUY"
                       else notional - commission, 2)

    return {
        "symbol":      symbol,
        "side":        side,
        "size":        size,
        "fill_price":  fill_price,
        "notional":    notional,
        "commission":  commission,
        "net_cost":    net_cost,
        "sim_date":    target_date.isoformat(),
        "mode":        "backtest",
        "status":      "SIMULATED",
        "timestamp":   datetime.now().isoformat(),
        "meta": {
            **candidate.get("meta", {}),
            "stage":           "execute",
            "_convention":     "1.0",
            "backtest_start":  START_DATE_STR,
            "backtest_end":    END_DATE_STR,
            "slip_bps":        SLIP_BPS,
            "commission_per_share": COMMISSION,
        }
    }


def run_backtest(candidates: list) -> dict:
    """
    Run candidates through the full date range, generating daily fills.
    Returns a summary dict + list of all receipts.
    """
    try:
        start = parse_date(START_DATE_STR)
        end   = parse_date(END_DATE_STR)
    except ValueError as e:
        print(f"[backtesting_harness] ERROR: {e}", file=sys.stderr)
        return {"error": str(e), "receipts": []}

    if start > end:
        print("[backtesting_harness] ERROR: BACKTEST_START is after BACKTEST_END",
              file=sys.stderr)
        return {"error": "start > end", "receipts": []}

    n_days    = date_range_days(start, end)
    n_cands   = len(candidates)

    print(f"[backtesting_harness] range: {start} → {end}  ({n_days} days)",
          file=sys.stderr)
    print(f"[backtesting_harness] candidates: {n_cands}  capital: ${CAPITAL:,.0f}",
          file=sys.stderr)

    # ── Simulate each candidate on the entry date (start) ────────────────
    receipts = []
    total_notional = 0.0
    total_pnl      = 0.0

    for c in candidates:
        if c.get("size", 0) == 0:
            print(f"[backtesting_harness] SKIP {c['symbol']} — size=0 "
                  f"(run size_positions before execute)", file=sys.stderr)
            continue

        # Entry fill on start date
        entry = simulate_order(c, start)

        # Exit fill on end date (simulate holding the entire period)
        exit_price = sim_price_at_date(c["symbol"], entry["fill_price"], end)
        exit_slip  = apply_slippage(exit_price,
                                    "SELL" if c["side"] == "BUY" else "BUY",
                                    SLIP_BPS)
        size    = c["size"]
        side    = c["side"]
        multiplier = 1.0 if side == "BUY" else -1.0
        period_pnl = round((exit_slip - entry["fill_price"]) * size * multiplier
                           - size * COMMISSION * 2, 2)
        period_ret = round(period_pnl / (entry["fill_price"] * size), 4) \
                     if entry["fill_price"] * size > 0 else 0.0

        entry["exit_price"]  = exit_slip
        entry["period_pnl"]  = period_pnl
        entry["period_return"] = period_ret
        entry["holding_days"]  = n_days

        receipts.append(entry)
        total_notional += entry["notional"]
        total_pnl      += period_pnl

        print(f"[backtesting_harness] {side:4} {size:4} {c['symbol']:6} "
              f"entry=${entry['fill_price']:.2f} "
              f"exit=${exit_slip:.2f} "
              f"pnl=${period_pnl:+,.0f} ({period_ret:+.2%})",
              file=sys.stderr)

    # ── Summary ───────────────────────────────────────────────────────────
    total_return = round(total_pnl / CAPITAL, 4) if CAPITAL > 0 else 0.0
    n_trades     = len(receipts)
    n_winners    = sum(1 for r in receipts if r.get("period_pnl", 0) > 0)
    win_rate     = round(n_winners / n_trades, 4) if n_trades > 0 else 0.0

    # Rough annualised Sharpe from period return
    ann_factor  = 252.0 / max(n_days, 1)
    ann_return  = total_return * ann_factor
    # Approximate vol from individual position returns
    pos_rets    = [r.get("period_return", 0) for r in receipts]
    if len(pos_rets) > 1:
        mean_r = sum(pos_rets) / len(pos_rets)
        var_r  = sum((r - mean_r) ** 2 for r in pos_rets) / len(pos_rets)
        vol    = var_r ** 0.5 * (252 ** 0.5)
        sharpe = round(ann_return / vol, 2) if vol > 0 else 0.0
    else:
        sharpe = 0.0

    summary = {
        "backtest_start":    START_DATE_STR,
        "backtest_end":      END_DATE_STR,
        "holding_days":      n_days,
        "capital":           CAPITAL,
        "n_trades":          n_trades,
        "n_winners":         n_winners,
        "win_rate":          win_rate,
        "total_pnl":         round(total_pnl, 2),
        "total_return":      total_return,
        "annualised_return": round(ann_return, 4),
        "sharpe":            sharpe,
        "total_notional":    round(total_notional, 2),
        "run_timestamp":     datetime.now().isoformat(),
    }

    return {"summary": summary, "receipts": receipts}


# ── Output & persistence ─────────────────────────────────────────────────
def save_results(result: dict) -> None:
    """Save backtest receipts and summary to LOGDIR."""
    os.makedirs(LOGDIR, exist_ok=True)

    receipts = result.get("receipts", [])
    summary  = result.get("summary", {})

    # Receipts CSV
    receipts_path = os.path.join(LOGDIR, "backtest_receipts.csv")
    if receipts:
        fields = ["symbol", "side", "size", "fill_price", "exit_price",
                  "notional", "commission", "period_pnl", "period_return",
                  "holding_days", "sim_date", "status"]
        with open(receipts_path, "w") as f:
            f.write(",".join(fields) + "\n")
            for r in receipts:
                row = ",".join(str(r.get(k, "")) for k in fields)
                f.write(row + "\n")
        print(f"[backtesting_harness] receipts → {receipts_path}", file=sys.stderr)

    # Summary CSV
    summary_path = os.path.join(LOGDIR, "backtest_harness_summary.csv")
    with open(summary_path, "w") as f:
        for k, v in summary.items():
            f.write(f"{k},{v}\n")
    print(f"[backtesting_harness] summary  → {summary_path}", file=sys.stderr)


def print_summary(summary: dict) -> None:
    """Pretty-print the backtest summary to stdout."""
    print("\n" + "═" * 60)
    print("  Las_shell Backtest Results")
    print("═" * 60)
    print(f"  Period     : {summary['backtest_start']} → {summary['backtest_end']}")
    print(f"  Days held  : {summary['holding_days']}")
    print(f"  Capital    : ${summary['capital']:,.0f}")
    print(f"  Trades     : {summary['n_trades']}  Winners: {summary['n_winners']}"
          f"  Win rate: {summary['win_rate']:.0%}")
    print(f"  Total PnL  : ${summary['total_pnl']:+,.2f}")
    print(f"  Return     : {summary['total_return']:+.2%}"
          f"  (ann. {summary['annualised_return']:+.2%})")
    print(f"  Sharpe     : {summary['sharpe']:.2f}")
    print("═" * 60 + "\n")


# ── Main entry point ─────────────────────────────────────────────────────
def main():
    # Verify we're in backtest mode
    if not BACKTEST_MODE:
        print("[backtesting_harness] WARNING: BACKTEST_MODE is not set to 1.",
              file=sys.stderr)
        print("[backtesting_harness] Set by the ~> operator or run with:",
              file=sys.stderr)
        print("[backtesting_harness]   BACKTEST_MODE=1 BACKTEST_START=YYYY-MM-DD "
              "BACKTEST_END=YYYY-MM-DD python3 scripts/backtesting_harness.py",
              file=sys.stderr)

    # Read candidates from stdin (pipeline convention v1.0)
    raw = sys.stdin.read().strip()

    if not raw or raw == "[]":
        # Standalone mode: generate a default candidate set for demonstration
        print("[backtesting_harness] no stdin — using demo candidates", file=sys.stderr)
        candidates = [
            {"symbol": s, "signal": sig, "size": sz, "price": px,
             "side": "BUY" if sig > 0 else "SELL",
             "meta": {"_convention": "1.0", "strategy": "demo", "stage": "universe"}}
            for s, sig, sz, px in [
                ("AAPL", 0.82,  100, 185.0),
                ("MSFT", 0.61,   45, 415.0),
                ("NVDA", 0.78,   20, 875.0),
                ("TSLA", -0.55,  80, 175.0),
                ("SPY",  0.44,   60, 510.0),
            ]
        ]
    else:
        try:
            candidates = json.loads(raw)
        except json.JSONDecodeError as e:
            print(f"[backtesting_harness] invalid JSON on stdin: {e}", file=sys.stderr)
            sys.exit(1)

    # Run the backtest
    result = run_backtest(candidates)

    if "error" in result:
        sys.exit(1)

    summary  = result["summary"]
    receipts = result["receipts"]

    # Print human-readable summary to stdout
    print_summary(summary)

    # Save detailed logs
    save_results(result)

    # Emit receipts as JSON to stdout (for |> audit chaining)
    print(json.dumps(receipts, indent=2))

    # Exit with useful code: 0 if Sharpe > 0, 1 otherwise
    sys.exit(0 if summary["sharpe"] >= 0 else 1)


if __name__ == "__main__":
    main()
