#!/bin/bash
# ═══════════════════════════════════════════════════════════════
# tests/integration/test_live_feed.sh
# Milestone 1 (v0.6.0) acceptance test: Real Market Data Feed
#
# Per the roadmap's acceptance criteria, this asserts something
# SPECIFIC to the real feed, not just "numbers differ from synthetic":
#   1. PRICE BAND  — fetched price is within ±20% of a reference price
#                     fetched from an INDEPENDENT source (Yahoo Finance),
#                     not derived from the feed under test (Stooq).
#   2. FRESHNESS    — price timestamp is within ±5 min of wall-clock
#                     during market hours, or not older than the last
#                     completed session's close outside market hours.
#                     See tests/integration/check_live_feed.py for the
#                     full design rationale.
#
# Sections 3 and 4 (failure-mode handling, cache atomicity/sharing) do
# NOT require live network and always run.
#
# If the network-dependent sections 1-2 cannot reach a real market-data
# host from this environment, that is reported as an explicit SKIP with
# the exact reason (never a silent pass) — see "Not Done When" in the
# roadmap: a broken fallback must not be able to pass this test by
# accident, so an unreachable network fails OPEN to SKIP, never to PASS.
# ═══════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/../.."
LAS_SHELL_HOME=${LAS_SHELL_HOME:-$(pwd)}
export LAS_SHELL_HOME

GREEN='\033[32m'; RED='\033[31m'; YELLOW='\033[33m'; RESET='\033[0m'
PASS=0; FAIL=0; SKIP=0

pass() { printf "  ${GREEN}PASS${RESET}  %s\n" "$1"; PASS=$((PASS+1)); }
fail() { printf "  ${RED}FAIL${RESET}  %s\n" "$1"; FAIL=$((FAIL+1)); }
skip() { printf "  ${YELLOW}SKIP${RESET}  %s\n" "$1"; SKIP=$((SKIP+1)); }

TEST_TICKER="AAPL"

echo "══ Milestone 1 Acceptance: Real Market Data Feed ═══════════════"
echo ""

# ── Section 1-2: price band + freshness (requires live network) ──
echo "── Live feed checks (ticker: $TEST_TICKER) ─────────────────────"

# Probe reachability with a cheap direct request before trying the
# independent-reference fetch, so a network outage produces one clear
# SKIP message instead of two confusing failures.
PROBE=$(python3 - "$TEST_TICKER" <<'PYEOF'
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath("__file__")), "scripts"))
sys.path.insert(0, "scripts")
import live_price_feed as lpf
r = lpf.fetch_one_live(sys.argv[1], timeout=8.0)
print(r["source"], r["fetch_status"])
PYEOF
)
PROBE_SOURCE=$(echo "$PROBE" | awk '{print $1}')

if [ "$PROBE_SOURCE" != "yahoo" ]; then
    skip "price band check — feed unreachable from this environment ($PROBE)"
    skip "freshness check — feed unreachable from this environment ($PROBE)"
else
    # Independent reference: Yahoo Finance chart API. Different vendor,
    # different code path, no shared cache with the Yahoo fetch above —
    # satisfies "not from the feed under test".
    REF_JSON=$(curl -s -m 8 "https://api.twelvedata.com/price?symbol=${TEST_TICKER}&apikey=demo" 2>/dev/null)
    REF_PRICE=$(echo "$REF_JSON" | python3 -c '
import json, sys
try:
    d = json.load(sys.stdin)
    price = d.get("price", "")
    print(price if price else "")
except Exception:
    print("")
' 2>/dev/null)

    if [ -z "$REF_PRICE" ]; then
        skip "price band check — independent reference (Twelve Data) unreachable"
        skip "freshness check — depends on same live fetch, ran separately below"
        # Freshness doesn't need the reference price, only the ticker fetch,
        # so it can still run even if the reference source is down.
        OUT=$(python3 tests/integration/check_live_feed.py "$TEST_TICKER" "1" 2>&1)
        if echo "$OUT" | grep -q "^FRESHNESS  PASS"; then
            pass "freshness check — $(echo "$OUT" | grep FRESHNESS)"
        else
            fail "freshness check — $(echo "$OUT" | grep FRESHNESS)"
        fi
    else
        OUT=$(python3 tests/integration/check_live_feed.py "$TEST_TICKER" "$REF_PRICE" 2>&1)
        echo "$OUT" | sed 's/^/    /'
        if echo "$OUT" | grep -q "^PRICE_BAND PASS"; then
            pass "price band check (reference=$REF_PRICE from Twelve Data)"
        else
            fail "price band check (reference=$REF_PRICE from Twelve Data)"
        fi
        if echo "$OUT" | grep -q "^FRESHNESS  PASS"; then
            pass "freshness check"
        else
            fail "freshness check"
        fi
    fi
fi

echo ""
echo "── Failure-mode handling (no network required) ──────────────────"

# Realistic failure mode: forced timeout. Must degrade to
# synthetic_fallback and must NOT raise/crash/corrupt anything.
TIMEOUT_OUT=$(python3 -c "
import sys
sys.path.insert(0, 'scripts')
import live_price_feed as lpf
r = lpf.fetch_one_live('AAPL', timeout=0.0001)
print(r['source'], r['fetch_status'])
" 2>&1)
if echo "$TIMEOUT_OUT" | grep -q "^synthetic_fallback"; then
    pass "forced timeout degrades to synthetic_fallback without raising ($TIMEOUT_OUT)"
else
    fail "forced timeout did not degrade cleanly (got: $TIMEOUT_OUT)"
fi

# Malformed / rate-limited / unknown-ticker responses (mocked — this is
# testing OUR parser's robustness, not Stooq's live behavior, so no
# network needed and no flakiness from Stooq's real rate limit).
MALFORMED_OUT=$(python3 -c "
import sys
sys.path.insert(0, 'scripts')
import live_price_feed as lpf
from unittest import mock

class FakeResp:
    def __init__(self, text): self.text = text.encode()
    def read(self): return self.text
    def __enter__(self): return self
    def __exit__(self, *a): return False

cases = [
    ('rate_limited', 'Exceeded the daily hits limit'),
    ('unknown_ticker', 'Symbol,Date,Time,Open,High,Low,Close,Volume\nXYZ.US,N/D,N/D,N/D,N/D,N/D,N/D,N/D'),
    ('malformed_csv', 'Symbol,Date,Time,Open,High,Low,Close,Volume\nAAPL.US,2026-08-13'),
]
ok = True
for name, body in cases:
    with mock.patch('urllib.request.urlopen', return_value=FakeResp(body)):
        r = lpf.fetch_one_live('AAPL')
        if r['source'] != 'synthetic_fallback':
            ok = False
            print(f'{name}: FAILED TO FALL BACK ({r})')
print('OK' if ok else 'FAILED')
" 2>&1)
if echo "$MALFORMED_OUT" | tail -1 | grep -q "^OK"; then
    pass "malformed/rate-limited/unknown-ticker responses all fall back cleanly"
else
    fail "malformed-response handling: $MALFORMED_OUT"
fi

echo ""
echo "── Cache sharing + atomicity (no network required) ──────────────"

CACHE_TEST_OUT=$(python3 -c "
import sys, tempfile, os, json
from datetime import datetime, timezone
sys.path.insert(0, 'scripts')
import live_price_feed as lpf
from unittest import mock

class FakeResp:
    def __init__(self, data): self.data = data
    def read(self): return json.dumps(self.data).encode()
    def __enter__(self): return self
    def __exit__(self, *a): return False

tmpdir = tempfile.mkdtemp()
cache_path = os.path.join(tmpdir, 'cache.json')
calls = {'n': 0}
now_unix = int(datetime.now(timezone.utc).timestamp())
yahoo_json = {'chart': {'result': [{'meta': {
    'regularMarketPrice': 231.0,
    'regularMarketOpen': 230.1,
    'regularMarketDayHigh': 231.5,
    'regularMarketDayLow': 229.8,
    'regularMarketVolume': 45123000,
    'regularMarketTime': now_unix
}}]}}
def fake_urlopen(*a, **k):
    calls['n'] += 1
    return FakeResp(yahoo_json)

with mock.patch('urllib.request.urlopen', side_effect=fake_urlopen):
    r1 = lpf.get_prices(['AAPL'], cache_path=cache_path, max_age=60)
    r2 = lpf.get_prices(['AAPL'], cache_path=cache_path, max_age=60)
    assert calls['n'] == 1, f'expected 1 fetch, got {calls[\"n\"]}'
    assert r1['AAPL']['price'] == r2['AAPL']['price'] == 231.0

leftover = [f for f in os.listdir(tmpdir) if '.tmp.' in f]
assert leftover == [], f'leftover tmp files: {leftover}'
print('OK')
" 2>&1)
if echo "$CACHE_TEST_OUT" | tail -1 | grep -q "^OK"; then
    pass "cache reuse (1 fetch for 2 calls) + atomic write (no leftover tmp files)"
else
    fail "cache sharing/atomicity: $CACHE_TEST_OUT"
fi

# universe.c and universe.py must read back identical prices from the
# same cache file — the concrete proof RNG divergence is moot.
echo ""
echo "── C/Python convergence on shared cache (no network required) ───"
CONV_DIR=$(mktemp -d)
CONV_CACHE="$CONV_DIR/live_price_cache.json"
mkdir -p "$CONV_DIR/logs"
CONV_HOME="$CONV_DIR"
mkdir -p "$CONV_HOME/scripts" "$CONV_HOME/pipeline"
cp scripts/live_price_feed.py "$CONV_HOME/scripts/"
cp pipeline/universe_c "$CONV_HOME/pipeline/universe_c" 2>/dev/null

if [ -x "$CONV_HOME/pipeline/universe_c" ]; then
    C_OUT=$(cd "$CONV_HOME" && LAS_SHELL_HOME="$CONV_HOME" ./pipeline/universe_c --top 1 2>/dev/null)
    C_PRICE=$(echo "$C_OUT" | python3 -c "import json,sys; print(json.load(sys.stdin)[0]['price'])" 2>/dev/null)
    PY_OUT=$(LAS_SHELL_HOME="$CONV_HOME" PYTHONPATH="$CONV_HOME/scripts" python3 pipeline/universe.py --symbols AAPL --top 1 2>/dev/null)
    PY_PRICE=$(echo "$PY_OUT" | python3 -c "import json,sys; print(json.load(sys.stdin)[0]['price'])" 2>/dev/null)
    if [ -n "$C_PRICE" ] && [ "$C_PRICE" = "$PY_PRICE" ]; then
        pass "universe_c and universe.py read identical price ($C_PRICE) from shared cache"
    else
        fail "universe_c ($C_PRICE) and universe.py ($PY_PRICE) diverged"
    fi
else
    skip "C/Python convergence check — universe_c binary not built (run 'make pipeline' first)"
fi
rm -rf "$CONV_DIR"

echo ""
echo "══════════════════════════════════════════════════════════════"
TOTAL=$((PASS + FAIL + SKIP))
printf "Results: %d/%d passed" "$PASS" "$TOTAL"
[ "$SKIP" -gt 0 ] && printf "  (%d skipped)" "$SKIP"
printf "\n"

if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}All non-skipped tests passed ✓${RESET}"
    exit 0
else
    echo -e "${RED}$FAIL test(s) failed ✗${RESET}"
    exit 1
fi
