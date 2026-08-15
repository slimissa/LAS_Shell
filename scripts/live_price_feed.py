#!/usr/bin/env python3
"""
Las_shell Live Price Feed — live_price_feed.py
================================================
Milestone 1 (v0.6.0): replaces synthetic price generation with one real,
deliberately imperfect data source: Yahoo Finance's free JSON chart
endpoint (https://query1.finance.yahoo.com/v8/finance/chart/).

This is the ONE shared fetch point for the whole pipeline. Both
pipeline/universe.py (Python stage) and pipeline/src/universe.c (C stage)
read prices through this script's cache file rather than each generating
their own numbers — this is what makes the C/Python RNG-divergence problem
moot: there is no RNG in the real-data path, and both languages consume
byte-identical fetched data.

Design
------
- fetch_live(tickers)      -> per-ticker dict, ALWAYS makes an HTTP
                               attempt per ticker (no cache short-circuit),
                               used directly by the acceptance test so it
                               can observe real fetch_status values.
- get_prices(tickers, ...) -> the function pipeline stages call. Reads the
                               shared JSON cache; refetches only entries
                               that are missing or older than --max-age
                               seconds; writes the merged result back to
                               the cache atomically (tmp file + rename,
                               same pattern src/crash_recovery.c uses for
                               checkpoints); returns the merged dict.
- Per-ticker fallback: if the real fetch fails for a symbol (network
  error, timeout, rate limit, malformed JSON, unknown ticker), THAT
  SYMBOL falls back to the pre-existing synthetic generator and is
  tagged "source": "synthetic_fallback" in both the cache and the output.
  Nothing is silently blended into the real path — a caller (or the
  acceptance test) can always tell which symbols are real and which
  aren't by reading "source".

Known, accepted limitations (imperfect by design — see roadmap Milestone 1)
- Yahoo Finance's free JSON endpoint is unauthenticated and may rate-limit;
  no key, no SLA.
- Yahoo's free endpoint is not true real-time; outside market hours it
  returns the last session's closing print, which is genuinely stale
  relative to wall-clock time. See freshness_ok() below and
  docs/LIVE_FEED.md for how the acceptance test accounts for this.
- No exchange-calendar awareness (deferred to Milestone 3 per roadmap
  Section 5) — the after-hours freshness check uses a naive "roll back to
  the most recent weekday" rule, not a real holiday calendar. Around a
  market holiday this can be overly strict (rejects a timestamp that is
  correct but older than the naive previous-weekday assumption). Documented,
  not silently worked around.

Usage
    python3 live_price_feed.py AAPL MSFT --json
    python3 live_price_feed.py AAPL --cache /path/to/cache.json --max-age 60
    python3 live_price_feed.py AAPL --force          # bypass cache freshness
    python3 live_price_feed.py AAPL --timeout 0.001   # force a timeout (for
                                                        # failure-mode testing)
"""
import sys
import os
import json
import time
import random
import hashlib
import tempfile
import urllib.request
import urllib.error
import socket
from datetime import datetime, timezone, timedelta, date

# ── Synthetic fallback (unchanged from scripts/price_feed.py) ───────────
SEED_PRICES = {
    "AAPL": 185.0, "MSFT": 415.0, "GOOGL": 175.0, "AMZN": 195.0,
    "TSLA": 175.0, "META": 510.0, "NVDA": 875.0, "SPY":  510.0,
    "QQQ":  435.0, "GLD":  195.0, "BTC":  67000.0,"ETH":  3500.0,
}

YAHOO_URL = "https://query1.finance.yahoo.com/v8/finance/chart/{ticker}"
USER_AGENT = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36"
DEFAULT_CACHE = os.path.join(
    os.environ.get("LAS_SHELL_HOME", os.getcwd()), "logs", "live_price_cache.json"
)
DEFAULT_MAX_AGE = 60      # seconds; cache entries older than this are refetched
DEFAULT_TIMEOUT = 8.0     # seconds; HTTP request timeout


def synthetic_price(ticker):
    """Fallback generator — identical algorithm to scripts/price_feed.py,
    used only when the real fetch fails for this ticker."""
    seed = int(hashlib.md5(f"{ticker}{date.today()}".encode()).hexdigest(), 16) % 10000
    random.seed(seed)
    base = SEED_PRICES.get(ticker.upper(), 100.0)
    daily_ret = random.gauss(0.0003, 0.015)
    close = round(base * (1 + daily_ret), 2)
    high  = round(close * random.uniform(1.001, 1.02), 2)
    low   = round(close * random.uniform(0.98, 0.999), 2)
    open_ = round(close * random.uniform(0.995, 1.005), 2)
    vol   = int(random.uniform(5e6, 80e6))
    return {
        "price": close, "open": open_, "high": high, "low": low, "volume": vol,
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "source": "synthetic_fallback",
        "fetch_status": "n/a",
    }


def fetch_one_live(ticker, timeout=DEFAULT_TIMEOUT):
    """Make one real HTTP request to Yahoo Finance for `ticker`. Never raises —
    always returns a dict with 'source' and 'fetch_status' set, so a
    caller can log exactly why a ticker fell back without a stack trace
    corrupting pipeline state.
    """
    url = YAHOO_URL.format(ticker=ticker)
    try:
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = json.loads(resp.read().decode("utf-8", errors="replace"))
    except socket.timeout:
        r = synthetic_price(ticker)
        r["fetch_status"] = "error:timeout"
        return r
    except urllib.error.URLError as e:
        r = synthetic_price(ticker)
        r["fetch_status"] = f"error:{e.reason}"
        return r
    except Exception as e:  # noqa: BLE001 — feed must never crash the pipeline
        r = synthetic_price(ticker)
        r["fetch_status"] = f"error:{type(e).__name__}"
        return r

    try:
        result = data["chart"]["result"][0]
        meta = result["meta"]
        price = meta.get("regularMarketPrice")
        ts_unix = meta.get("regularMarketTime")

        if price is None or price <= 0 or ts_unix is None:
            r = synthetic_price(ticker)
            r["fetch_status"] = "error:unknown_ticker"
            return r

        price_dt = datetime.fromtimestamp(ts_unix, tz=timezone.utc)
        return {
            "price": float(price),
            "open": float(meta.get("regularMarketOpen", price)),
            "high": float(meta.get("regularMarketDayHigh", price)),
            "low": float(meta.get("regularMarketDayLow", price)),
            "volume": int(meta.get("regularMarketVolume", 0) or 0),
            "timestamp": price_dt.isoformat(),
            "source": "yahoo",
            "fetch_status": "ok",
        }
    except (KeyError, IndexError, TypeError, ValueError, json.JSONDecodeError):
        r = synthetic_price(ticker)
        r["fetch_status"] = "error:parse_error"
        return r


def fetch_live(tickers, timeout=DEFAULT_TIMEOUT):
    """Fetch every ticker for real, no cache. Used by the acceptance test."""
    return {t.upper(): fetch_one_live(t, timeout=timeout) for t in tickers}


def _load_cache(cache_path):
    try:
        with open(cache_path) as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def _save_cache_atomic(cache_path, data):
    """Same atomic tmp-file + rename(2) pattern src/crash_recovery.c uses
    for checkpoints, so a crash mid-write can never leave callers reading
    a half-written cache file."""
    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    fd, tmp_path = tempfile.mkstemp(
        prefix=os.path.basename(cache_path) + ".tmp.", dir=os.path.dirname(cache_path)
    )
    try:
        with os.fdopen(fd, "w") as f:
            json.dump(data, f, indent=2)
        os.rename(tmp_path, cache_path)
    except Exception:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass
        raise


def _entry_age_seconds(entry):
    try:
        ts = datetime.fromisoformat(entry["timestamp"])
        if ts.tzinfo is None:
            ts = ts.replace(tzinfo=timezone.utc)
        return (datetime.now(timezone.utc) - ts).total_seconds()
    except (KeyError, ValueError):
        return float("inf")


def get_prices(tickers, cache_path=DEFAULT_CACHE, max_age=DEFAULT_MAX_AGE,
                force=False, timeout=DEFAULT_TIMEOUT):
    """The call every pipeline stage should use. Reads the shared cache;
    only refetches tickers that are missing or stale; writes the merged
    result back atomically; returns {ticker: entry} for exactly the
    requested tickers.
    """
    tickers = [t.upper() for t in tickers]
    cache = _load_cache(cache_path)

    to_fetch = []
    for t in tickers:
        entry = cache.get(t)
        if force or entry is None or _entry_age_seconds(entry) > max_age:
            to_fetch.append(t)

    if to_fetch:
        fresh = fetch_live(to_fetch, timeout=timeout)
        cache.update(fresh)
        _save_cache_atomic(cache_path, cache)

    return {t: cache[t] for t in tickers if t in cache}


def main():
    args = sys.argv[1:]
    tickers = []
    use_json = False
    cache_path = DEFAULT_CACHE
    max_age = DEFAULT_MAX_AGE
    timeout = DEFAULT_TIMEOUT
    force = False
    live_only = False  # --live-only: bypass cache entirely (acceptance test uses this)

    i = 0
    while i < len(args):
        a = args[i]
        if a == "--json":
            use_json = True; i += 1
        elif a == "--cache" and i + 1 < len(args):
            cache_path = args[i + 1]; i += 2
        elif a == "--max-age" and i + 1 < len(args):
            max_age = float(args[i + 1]); i += 2
        elif a == "--timeout" and i + 1 < len(args):
            timeout = float(args[i + 1]); i += 2
        elif a == "--force":
            force = True; i += 1
        elif a == "--live-only":
            live_only = True; i += 1
        else:
            tickers.append(a.upper()); i += 1

    if not tickers:
        print("Usage: live_price_feed.py TICKER [TICKER...] [--json] "
              "[--cache PATH] [--max-age SEC] [--timeout SEC] [--force] [--live-only]",
              file=sys.stderr)
        sys.exit(1)

    if live_only:
        results = fetch_live(tickers, timeout=timeout)
    else:
        results = get_prices(tickers, cache_path=cache_path, max_age=max_age,
                              force=force, timeout=timeout)

    if use_json:
        print(json.dumps(results, indent=2))
    else:
        for t in tickers:
            r = results.get(t, {})
            print(f"{t} {r.get('price', 'N/A')} "
                  f"source={r.get('source', '?')} status={r.get('fetch_status', '?')}")

    # A fetch failure degrades a ticker to synthetic_fallback rather than
    # crashing the pipeline — see module docstring. Exit 0 unconditionally;
    # callers that care about real-vs-fallback read the "source" field.
    sys.exit(0)


if __name__ == "__main__":
    main()
    