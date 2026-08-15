# Milestone 1 (v0.6.0) — Real Market Data Feed: Design Notes

## Data source
**Stooq** (`https://stooq.com/q/l/`), CSV quote endpoint, no API key.
Chosen over Yahoo Finance / Alpha Vantage / Tiingo because it needs zero
auth friction and returns a trivially parseable 2-line CSV. Traded off
against: no true real-time quotes outside market hours, undocumented
rate limits, occasional `N/D` fields for bad tickers.

## Decision 1 — Shared-cache read point
`scripts/live_price_feed.py` is the *only* code that speaks HTTP to a
market-data provider. It writes results to `logs/live_price_cache.json`
using the same atomic tmp-file + `rename(2)` pattern `src/crash_recovery.c`
uses for checkpoints.

- `pipeline/universe.py` imports `live_price_feed` directly and calls
  `get_prices()`.
- `pipeline/src/universe.c` does **not** reimplement HTTP/CSV parsing. It
  shells out to the same Python script (one process spawn per run, not
  per ticker) to refresh the cache, then reads the resulting JSON with a
  minimal scanner matching the idiom `pipeline/src/risk_filter.c` already
  uses (`extract_str`/`extract_num`).

Result: both language paths read the literal same cache entries for a
given run, so the pre-existing C/Python RNG divergence (different
seed/hash schemes) is structurally moot on the real-data path — verified
by `tests/integration/test_live_feed.sh`'s C/Python convergence check,
which asserts `universe_c` and `universe.py` produce an identical price
for the same symbol from the same cache.

## Decision 2 — Freshness check
Original roadmap wording: "timestamp within ±5 minutes of wall-clock."
This holds only while the market is open. Free feeds correctly return
the last session's closing print after hours — a real, correct feed can
be hours "stale" by that literal reading, and a naive implementation
either fails honestly after-hours or someone quietly loosens the rule to
pass, which defeats the point of the check.

Implemented rule (`tests/integration/check_live_feed.py::freshness_ok`):
- **During NYSE regular hours** (9:30–16:00 America/New_York, Mon–Fri):
  timestamp must be within ±5 minutes of wall-clock.
- **Outside those hours**: timestamp must fall between the most recent
  completed session's close (16:00 ET, rolled back to the most recent
  weekday) and now. Older than that, or in the future, still fails.

**Known limitation, stated rather than hidden:** the after-hours floor
uses a naive Mon–Fri rollback with no exchange holiday calendar
(calendar awareness is explicit Milestone 3 scope per the roadmap).
Around a market holiday this can be *overly strict* — it expects a
close print from a day the market was actually closed. This will show
up as a false FAIL, not a false PASS, so it fails safe.

## Failure modes handled
Per-ticker, not all-or-nothing — one bad symbol never takes down the
others:
- Network timeout / connection error → `synthetic_fallback`
- Rate limit ("Exceeded the daily hits limit" plain-text body instead of
  CSV) → `synthetic_fallback`
- Unknown ticker (`N/D` fields) → `synthetic_fallback`
- Malformed/truncated CSV row → `synthetic_fallback`

Every fallback is tagged in output (`meta.data_source`,
`meta.fetch_status` in JSON candidates; `source`/`fetch_status` in the
cache) — never silently blended with real data.

## What the acceptance test cannot verify in a network-restricted sandbox
`tests/integration/test_live_feed.sh` probes reachability first. If the
Stooq host is unreachable, the price-band and freshness checks report an
explicit `SKIP` with the exact reason — they never pass by accident on a
broken/absent feed. The failure-mode and cache-sharing/atomicity/C-Python
convergence checks do not need network and always run.
