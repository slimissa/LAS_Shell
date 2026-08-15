#!/usr/bin/env python3
"""
Las_shell Milestone 1 acceptance checks — check_live_feed.py
==============================================================
Implements the two checks the roadmap requires, callable independently
so tests/integration/test_live_feed.sh (and CI) can assert on them
directly rather than re-deriving the logic in bash.

Design decision (resolved before implementation, per user instruction):

1. Shared-cache read point: this script does NOT read the pipeline's
   cache (scripts/live_price_feed.py's live_price_cache.json). It calls
   fetch_live() with --live-only semantics itself, bypassing the cache,
   so the acceptance test is checking a fresh, direct fetch — not
   something the pipeline might have cached minutes ago.

2. Freshness check: the original roadmap wording ("timestamp within
   ±5 minutes of wall-clock") only holds during live trading. Outside
   NYSE regular hours (9:30-16:00 America/New_York, Mon-Fri), a correct
   real feed returns the last session's closing print, which is
   legitimately older than 5 minutes. Replacing that unconditionally
   would let a broken feed hide behind "well it's after hours" — so
   the rule here is:

     - During regular market hours: timestamp must be within ±5 minutes
       of wall-clock (proves live data, not a stale cache).
     - Outside market hours: timestamp must be >= the most recent
       completed session's close (16:00 ET, rolled back to the most
       recent weekday) AND <= now. A timestamp older than that, or in
       the future, still fails.

   Known limitation, stated rather than hidden: this uses a naive
   weekday rollback with NO exchange holiday calendar (calendar
   awareness is explicit Milestone 3 scope per the roadmap). Around a
   market holiday this can be overly strict — it will expect a close
   timestamp from a day the market was actually closed. Documented in
   docs/LIVE_FEED.md, not silently patched around here.
"""
import sys
import os
from datetime import datetime, timezone, timedelta

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts"))
import live_price_feed as lpf

try:
    from zoneinfo import ZoneInfo
    NY = ZoneInfo("America/New_York")
except Exception:
    NY = None


def is_market_hours(now_utc):
    if NY is None:
        return False  # can't determine — treat conservatively as closed
    local = now_utc.astimezone(NY)
    if local.weekday() >= 5:  # Sat/Sun
        return False
    open_t = local.replace(hour=9, minute=30, second=0, microsecond=0)
    close_t = local.replace(hour=16, minute=0, second=0, microsecond=0)
    return open_t <= local <= close_t


def last_session_close_utc(now_utc):
    """Most recent 16:00 America/New_York on a weekday at or before now.
    No holiday calendar — see module docstring."""
    if NY is None:
        return now_utc - timedelta(days=4)  # conservative fallback
    local = now_utc.astimezone(NY)
    candidate = local.replace(hour=16, minute=0, second=0, microsecond=0)
    if candidate > local:
        candidate -= timedelta(days=1)
    while candidate.weekday() >= 5:
        candidate -= timedelta(days=1)
    return candidate.astimezone(timezone.utc)


def freshness_ok(price_timestamp_iso, now_utc=None):
    now_utc = now_utc or datetime.now(timezone.utc)
    ts = datetime.fromisoformat(price_timestamp_iso)
    if ts.tzinfo is None:
        ts = ts.replace(tzinfo=timezone.utc)

    if is_market_hours(now_utc):
        delta = abs((now_utc - ts).total_seconds())
        ok = delta <= 300
        reason = f"market hours: |now - ts| = {delta:.0f}s (limit 300s)"
    else:
        floor = last_session_close_utc(now_utc)
        ok = floor <= ts <= now_utc
        reason = (f"after hours: ts={ts.isoformat()} must be within "
                  f"[{floor.isoformat()}, {now_utc.isoformat()}]")
    return ok, reason


def price_band_ok(test_price, reference_price, band_pct=0.20):
    if reference_price <= 0:
        return False, "reference price <= 0, cannot compute band"
    lo = reference_price * (1 - band_pct)
    hi = reference_price * (1 + band_pct)
    ok = lo <= test_price <= hi
    return ok, f"band [{lo:.2f}, {hi:.2f}] (±{band_pct*100:.0f}% of {reference_price:.2f}), got {test_price:.2f}"


def main():
    if len(sys.argv) < 3:
        print("Usage: check_live_feed.py TICKER REFERENCE_PRICE [--timeout SEC]", file=sys.stderr)
        sys.exit(2)

    ticker = sys.argv[1].upper()
    reference_price = float(sys.argv[2])
    timeout = 8.0
    if "--timeout" in sys.argv:
        timeout = float(sys.argv[sys.argv.index("--timeout") + 1])

    entry = lpf.fetch_one_live(ticker, timeout=timeout)

    if entry["source"] != "yahoo":
        print(f"NOT_REAL source={entry['source']} fetch_status={entry['fetch_status']}")
        sys.exit(3)  # distinct code: fetch didn't get real data (network/parse issue)

    band_ok, band_reason = price_band_ok(entry["price"], reference_price)
    fresh_ok, fresh_reason = freshness_ok(entry["timestamp"])

    print(f"price={entry['price']} timestamp={entry['timestamp']} source={entry['source']}")
    print(f"PRICE_BAND {'PASS' if band_ok else 'FAIL'}: {band_reason}")
    print(f"FRESHNESS  {'PASS' if fresh_ok else 'FAIL'}: {fresh_reason}")

    sys.exit(0 if (band_ok and fresh_ok) else 1)


if __name__ == "__main__":
    main()
