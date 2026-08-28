#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# test_multi_currency_pnl.sh — Las_shell v0.6.0 Milestone 3 Phase 7
#
# Dedicated coverage for the multi-currency P&L work: broker.c's
# per-currency grouping in paper_print_positions()/positions --json, and
# pnl_report.py's aggregation via the real ledger.
#
# This exists because Phase 7 shipped with only manual/ad-hoc verification
# (shown in the session transcript, not committed as a repeatable test) --
# flagged as a gap and closed here, same principle as roadmap Tier 2 §6.3
# ("commit acceptance tests as automated checks, not report notes").
#
# What this specifically guards against regressing:
#   1. Every position defaults to a real currency tag ("USD" absent a
#      configured BASE_CURRENCY), not an empty/garbage field.
#   2. The headline TOTAL only sums positions in the base currency --
#      the bug fixed mid-session where tot_mv/tot_up summed ALL
#      positions as raw doubles regardless of currency (nonsensical
#      the moment more than one currency is present).
#   3. The "By currency" breakdown appears only when genuinely more
#      than one currency exists (not for the common single-currency
#      case), uses correct per-currency decimal places / symbol via
#      currency.c, and explicitly labels non-base groups as
#      unconverted rather than silently folding them into the total
#      or inventing an FX rate.
#   4. pnl_report.py reads the real ledger (not synthetic data) when
#      one exists, reports the same base-currency-only total broker.c
#      would, and surfaces other-currency positions the same way.
#   5. pnl_report.py's synthetic fallback still fires, and is clearly
#      labeled, when no real ledger is reachable.
#
# Usage: ./test_multi_currency_pnl.sh
# Exit: 0 = all tests passed, non-zero = failures
# ═══════════════════════════════════════════════════════════════════════════
set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$PROJECT_ROOT"
export LAS_SHELL_HOME="$PROJECT_ROOT"

SHELL_BIN="./las_shell"
PASS=0
FAIL=0

GREEN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'

assert_contains() {
    local label="$1" haystack="$2" needle="$3"
    if [[ "$haystack" == *"$needle"* ]]; then
        printf "  ${GREEN}PASS${NC}  %s\n" "$label"; PASS=$((PASS+1))
    else
        printf "  ${RED}FAIL${NC}  %s\n" "$label"
        printf "         Expected to contain: %s\n" "$needle"
        FAIL=$((FAIL+1))
    fi
}
assert_not_contains() {
    local label="$1" haystack="$2" needle="$3"
    if [[ "$haystack" != *"$needle"* ]]; then
        printf "  ${GREEN}PASS${NC}  %s\n" "$label"; PASS=$((PASS+1))
    else
        printf "  ${RED}FAIL${NC}  %s\n" "$label"
        printf "         Expected NOT to contain: %s\n" "$needle"
        FAIL=$((FAIL+1))
    fi
}
assert_eq() {
    local label="$1" got="$2" want="$3"
    if [[ "$got" == "$want" ]]; then
        printf "  ${GREEN}PASS${NC}  %s\n" "$label"; PASS=$((PASS+1))
    else
        printf "  ${RED}FAIL${NC}  %s\n" "$label"
        printf "         got=%s want=%s\n" "$got" "$want"
        FAIL=$((FAIL+1))
    fi
}

# ── Preserve/restore the user's real paper account + risk config ──
PAPERFILE="$HOME/.las_shell_paper_account"
RISKFILE="$HOME/.las_shell_risk"
OLD_PAPER=""; OLD_RISK=""
[[ -f "$PAPERFILE" ]] && OLD_PAPER=$(cat "$PAPERFILE")
[[ -f "$RISKFILE"  ]] && OLD_RISK=$(cat "$RISKFILE")
cleanup() {
    if [[ -n "$OLD_PAPER" ]]; then echo "$OLD_PAPER" > "$PAPERFILE"; else rm -f "$PAPERFILE"; fi
    if [[ -n "$OLD_RISK"  ]]; then echo "$OLD_RISK"  > "$RISKFILE";  else rm -f "$RISKFILE";  fi
    rm -f "$HOME/.las_shell_pnl_report"
}
trap cleanup EXIT

echo "══ Milestone 3 Phase 7: Multi-Currency P&L ═══════════════════════"

# ── 1. Single-currency case (the common, real-today case) ──
echo ""
echo "── 1. Single-currency (USD) positions ──"
rm -f "$PAPERFILE"
$SHELL_BIN -c "reset_paper --yes" > /dev/null
$SHELL_BIN -c "order buy AAPL 10 market" > /dev/null
OUT=$($SHELL_BIN -c "positions")
assert_contains  "positions text output shows AAPL"        "$OUT" "AAPL"
assert_not_contains "no 'By currency' section for single-currency case" "$OUT" "By currency"

JOUT=$($SHELL_BIN -c "positions --json")
assert_contains "positions --json includes currency field" "$JOUT" "\"currency\":\"USD\""

# ── 2. Multi-currency case (injected -- no live mechanism creates a
#    non-USD position today, so this is how the grouping/exclusion
#    logic gets exercised at all) ──
echo ""
echo "── 2. Multi-currency positions (injected EUR position) ──"
echo "pos.SAP=5:150.0000:0.0000:EUR" >> "$PAPERFILE"

OUT=$($SHELL_BIN -c "positions")
assert_contains "By currency section appears with 2+ currencies" "$OUT" "By currency"
assert_contains "USD line present in breakdown"                  "$OUT" "USD   mkt_val="
assert_contains "EUR line present in breakdown"                  "$OUT" "EUR   mkt_val="
assert_contains "EUR line uses euro symbol"                      "$OUT" "€"
assert_contains "EUR line labeled unconverted"                   "$OUT" "no FX rate source configured"
assert_contains "EUR line explicitly excluded from TOTAL"        "$OUT" "NOT included in the USD TOTAL"

# The critical correctness check: TOTAL must equal the USD-only sum,
# not USD+EUR mixed as raw numbers. AAPL market value alone (10 sh)
# should match the TOTAL row exactly, since SAP/EUR must be excluded.
TOTAL_LINE=$(echo "$OUT" | grep "^  TOTAL")
AAPL_MV=$(echo "$OUT" | grep "^  AAPL" | awk '{print $5}')
TOTAL_MV=$(echo "$TOTAL_LINE" | awk '{print $2}')
assert_eq "TOTAL market value == AAPL-only market value (EUR excluded)" \
          "$TOTAL_MV" "$AAPL_MV"

JOUT=$($SHELL_BIN -c "positions --json")
assert_contains "positions --json includes EUR currency tag" "$JOUT" "\"currency\":\"EUR\""

# ── 3. BASE_CURRENCY changes which group is "the total" ──
echo ""
echo "── 3. BASE_CURRENCY=EUR flips which group is unconverted ──"
echo "BASE_CURRENCY = EUR" > "$RISKFILE"
OUT=$($SHELL_BIN -c "positions")
assert_contains "USD now labeled unconverted when BASE_CURRENCY=EUR" \
                 "$OUT" "NOT included in the EUR TOTAL"
rm -f "$RISKFILE"

# ── 4. pnl_report.py against the same real, injected ledger ──
echo ""
echo "── 4. pnl_report.py real-ledger path ──"
PYOUT=$(python3 scripts/pnl_report.py 2>&1)
assert_not_contains "pnl_report.py text output is NOT flagged synthetic" \
                     "$PYOUT" "SYNTHETIC"
assert_contains "pnl_report.py surfaces the EUR position separately" \
                 "$PYOUT" "EUR:"
assert_contains "pnl_report.py labels it unconverted" \
                 "$PYOUT" "NOT in total above"

PYJSON=$(python3 scripts/pnl_report.py --format json 2>&1)
assert_contains "pnl_report.py --format json: synthetic is false" \
                 "$PYJSON" "\"synthetic\": false"
assert_contains "pnl_report.py --format json: other_currencies has EUR" \
                 "$PYJSON" "\"EUR\""
assert_contains "pnl_report.py --format json: EUR marked not converted" \
                 "$PYJSON" "\"converted\": false"

# ── 5. pnl_report.py synthetic fallback still works when unreachable ──
echo ""
echo "── 5. pnl_report.py synthetic fallback (no real ledger reachable) ──"
FALLBACK_JSON=$(LAS_SHELL_HOME=/nonexistent_dir_for_test python3 scripts/pnl_report.py --format json 2>&1)
assert_contains "fallback path is explicitly marked synthetic" \
                 "$FALLBACK_JSON" "\"synthetic\": true"
FALLBACK_TEXT=$(LAS_SHELL_HOME=/nonexistent_dir_for_test python3 scripts/pnl_report.py 2>&1)
assert_contains "fallback text output visibly warns it's synthetic" \
                 "$FALLBACK_TEXT" "SYNTHETIC"

rm -f "$PAPERFILE"

echo ""
echo "═══════════════════════════════════════════════════════════════"
TOTAL=$((PASS + FAIL))
printf "Results: %d/%d passed\n" "$PASS" "$TOTAL"
if [[ $FAIL -eq 0 ]]; then
    echo -e "${GREEN}All tests passed ✓${NC}"
    exit 0
else
    echo -e "${RED}$FAIL test(s) FAILED ✗${NC}"
    exit 1
fi
