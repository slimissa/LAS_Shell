#!/bin/bash
# ═══════════════════════════════════════════════════════════════
# QShell Strategy Template Library — Test Suite v2
# Fast tests — completes in < 15 seconds
# ═══════════════════════════════════════════════════════════════

BINARY=${1:-./las_shell}
QSHELL_HOME=${QSHELL_HOME:-$(pwd)}
PASS=0; FAIL=0; TOTAL=0
SCRIPTS="$QSHELL_HOME/scripts"
TEMPLATES="$QSHELL_HOME/templates"
LOGS="$QSHELL_HOME/logs"
mkdir -p "$LOGS"

GREEN='\033[32m'; RED='\033[31m'; RESET='\033[0m'
BOLD='\033[1m';   BLUE='\033[34m'; YELLOW='\033[33m'

ok()   { TOTAL=$((TOTAL+1)); PASS=$((PASS+1)); echo -e "  ${GREEN}✔${RESET} $1"; }
fail() { TOTAL=$((TOTAL+1)); FAIL=$((FAIL+1)); echo -e "  ${RED}✘${RESET} $1"; [ -n "$2" ] && echo -e "    ${BOLD}$2${RESET}"; }

run_py() {
    local desc="$1" cmd="$2" expect="$3" want="${4:-0}"
    local out; out=$(eval "$cmd" 2>&1); local got=$?
    TOTAL=$((TOTAL+1))
    local pass=1
    [ "$got" = "$want" ] || pass=0
    [ -z "$expect" ] || echo "$out" | grep -qF "$expect" || pass=0
    if [ $pass -eq 1 ]; then echo -e "  ${GREEN}✔${RESET} $desc"; PASS=$((PASS+1))
    else
        echo -e "  ${RED}✘${RESET} $desc"
        [ "$got" != "$want" ] && echo -e "    exit: got=$got want=$want"
        [ -n "$expect" ] && ! echo "$out" | grep -qF "$expect" && echo -e "    expected '$expect'"
        echo -e "    output: $(echo "$out" | head -2)"; FAIL=$((FAIL+1))
    fi
}

run_qs() {
    local desc="$1" script="$2" expect="$3" want="${4:-0}"
    local tmp; tmp=$(mktemp /tmp/qshell_XXXXXX.sh)
    printf '%s\n' "$script" > "$tmp"
    local out; out=$(QSHELL_HOME="$QSHELL_HOME" "$BINARY" "$tmp" 2>&1); local got=$?
    rm -f "$tmp"
    TOTAL=$((TOTAL+1))
    local pass=1
    [ "$got" = "$want" ] || pass=0
    [ -z "$expect" ] || echo "$out" | grep -qF "$expect" || pass=0
    if [ $pass -eq 1 ]; then echo -e "  ${GREEN}✔${RESET} $desc"; PASS=$((PASS+1))
    else
        echo -e "  ${RED}✘${RESET} $desc"
        [ "$got" != "$want" ] && echo -e "    exit: got=$got want=$want"
        [ -n "$expect" ] && echo -e "    expected: '$expect'"
        echo -e "    output: $(echo "$out" | head -2)"; FAIL=$((FAIL+1))
    fi
}

section() { echo ""; echo -e "${BLUE}━━━ $1 ━━━${RESET}"; }
part()    { echo ""; echo -e "${YELLOW}╔══════════════════════════════════════════╗${RESET}"
            printf  "${YELLOW}║  %-40s║${RESET}\n" "$1"
            echo -e "${YELLOW}╚══════════════════════════════════════════╝${RESET}"; }

echo ""; echo -e "${BOLD}QShell Template Library Test Suite v2${RESET}"
echo "Binary     : $BINARY"; echo "QSHELL_HOME: $QSHELL_HOME"; echo "Date       : $(date)"; echo ""

# ════════════════════════════════════════════════════════════════
part "PART 1 — File Structure (13 tests)"
# ════════════════════════════════════════════════════════════════
section "Templates"
[ -d "$TEMPLATES" ]                        && ok "templates/ dir"            || fail "templates/ dir missing"
[ -f "$TEMPLATES/momentum_daily.sh" ]      && ok "momentum_daily.sh"         || fail "momentum_daily.sh missing"
[ -f "$TEMPLATES/mean_reversion.sh" ]      && ok "mean_reversion.sh"         || fail "mean_reversion.sh missing"
[ -f "$TEMPLATES/market_make.sh" ]         && ok "market_make.sh"            || fail "market_make.sh missing"
[ -f "$TEMPLATES/backtest_runner.sh" ]     && ok "backtest_runner.sh"        || fail "backtest_runner.sh missing"

section "Scripts"
for s in price_feed.py momentum.py pairs_spread.py market_maker.py \
          risk_check.py order_executor.py pnl_report.py run_backtest.py; do
    [ -f "$SCRIPTS/$s" ] && ok "$s" || fail "$s missing"
done

# ════════════════════════════════════════════════════════════════
part "PART 2 — Python Scripts (28 tests)"
# ════════════════════════════════════════════════════════════════
section "price_feed.py"
run_py "single ticker"    "python3 $SCRIPTS/price_feed.py AAPL"                           "AAPL"
run_py "multi ticker"     "python3 $SCRIPTS/price_feed.py AAPL MSFT"                      "MSFT"
run_py "--field close"    "python3 $SCRIPTS/price_feed.py AAPL --field close"             "AAPL"
run_py "--field volume"   "python3 $SCRIPTS/price_feed.py AAPL --field volume"            "AAPL"
run_py "--json valid"     "python3 $SCRIPTS/price_feed.py AAPL --json | python3 -c 'import sys,json; print(json.load(sys.stdin)[0][\"ticker\"])'" "AAPL"
run_py "unknown ticker"   "python3 $SCRIPTS/price_feed.py XYZ123"                         "XYZ123"
run_py "no args → 1"      "python3 $SCRIPTS/price_feed.py" "" "1"

section "momentum.py"
run_py "BUY/SELL/HOLD"    "python3 $SCRIPTS/momentum.py AAPL MSFT GOOGL"                  "AAPL"
run_py "--topn 3"         "python3 $SCRIPTS/momentum.py AAPL MSFT GOOGL AMZN TSLA --topn 3 | wc -l | tr -d ' '" "3"
run_py "--lookback"       "python3 $SCRIPTS/momentum.py AAPL --lookback 10"               "AAPL"
run_py "signal format"    "python3 $SCRIPTS/momentum.py AAPL | grep -E '^AAPL (BUY|SELL|HOLD)'" "AAPL"
run_py "exit 0"           "python3 $SCRIPTS/momentum.py AAPL MSFT NVDA" "" "0"

section "pairs_spread.py"
run_py "ZSCORE present"   "python3 $SCRIPTS/pairs_spread.py AAPL MSFT; true"              "ZSCORE"
run_py "SIGNAL present"   "python3 $SCRIPTS/pairs_spread.py AAPL MSFT; true"              "SIGNAL"
run_py "PAIR present"     "python3 $SCRIPTS/pairs_spread.py AAPL MSFT; true"              "PAIR"
run_py "--entry flag"     "python3 $SCRIPTS/pairs_spread.py SPY QQQ --entry 1.5; true"    "PAIR"
run_py "no args → 2"      "python3 $SCRIPTS/pairs_spread.py AAPL" "" "2"

section "market_maker.py"
run_py "BID present"      "python3 $SCRIPTS/market_maker.py AAPL"                         "BID"
run_py "ASK present"      "python3 $SCRIPTS/market_maker.py AAPL"                         "ASK"
run_py "MID present"      "python3 $SCRIPTS/market_maker.py AAPL"                         "MID"
run_py "BID < ASK"        "python3 -c \"
import subprocess, re
o=subprocess.check_output(['python3','$SCRIPTS/market_maker.py','AAPL']).decode()
d=dict(x.split('=') for x in o.split() if '=' in x)
print('OK' if float(d['BID'])<float(d['ASK']) else 'FAIL')
\"" "OK"
run_py "no ticker → 1"   "python3 $SCRIPTS/market_maker.py" "" "1"

section "risk_check.py"
run_py "small order OK"   "echo 'AAPL BUY 100 185.50' | python3 $SCRIPTS/risk_check.py"   "RISK_PASSED" "0"
run_py "oversized → 1"    "echo 'AAPL BUY 99999 185.50' | python3 $SCRIPTS/risk_check.py" "" "1"
run_py "blacklist GME"    "echo 'GME BUY 100 20.00' | python3 $SCRIPTS/risk_check.py"     "" "1"
run_py "high notional"    "echo 'AAPL BUY 5000 185.50' | python3 $SCRIPTS/risk_check.py"  "" "1"
run_py "custom limit"     "echo 'AAPL BUY 100 185.50' | python3 $SCRIPTS/risk_check.py --max_notional 10000" "" "1"
run_py "empty stdin → 0"  "echo '' | python3 $SCRIPTS/risk_check.py" "" "0"

section "order_executor.py + pnl_report.py"
run_py "executor FILLED"  "echo 'AAPL BUY 100 185.50' | python3 $SCRIPTS/order_executor.py" "FILLED"
run_py "executor fill price" "echo 'MSFT BUY 50 415.00' | python3 $SCRIPTS/order_executor.py" "FILLED MSFT"
run_py "executor empty → 1" "echo '' | python3 $SCRIPTS/order_executor.py" "" "1"
run_py "pnl text format"  "QSHELL_HOME=$QSHELL_HOME python3 $SCRIPTS/pnl_report.py"       "P&L Report"
run_py "pnl json format"  "QSHELL_HOME=$QSHELL_HOME python3 $SCRIPTS/pnl_report.py --format json | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d[\"total_pnl\"])'" ""
run_py "pnl writes file"  "QSHELL_HOME=$QSHELL_HOME python3 $SCRIPTS/pnl_report.py>/dev/null && cat ~/.qshell_pnl" ""

# ════════════════════════════════════════════════════════════════
part "PART 3 — QShell Operator Integration (12 tests)"
# ════════════════════════════════════════════════════════════════
section "?> risk gate"
run_qs "small order passes ?>" \
    "setenv QSHELL_HOME $QSHELL_HOME
echo 'AAPL BUY 100 185.50' ?> python3 $SCRIPTS/risk_check.py && echo PASS" "PASS"

run_qs "oversized blocked by ?>" \
    "setenv QSHELL_HOME $QSHELL_HOME
echo 'AAPL BUY 99999 185.50' ?> python3 $SCRIPTS/risk_check.py || echo BLOCKED" "BLOCKED"

run_qs "blacklist GME blocked" \
    "setenv QSHELL_HOME $QSHELL_HOME
echo 'GME BUY 100 20.00' ?> python3 $SCRIPTS/risk_check.py || echo REJECTED" "REJECTED"

run_qs "?> fail && → blocked" \
    "setenv QSHELL_HOME $QSHELL_HOME
echo 'GME BUY 99999 20.00' ?> python3 $SCRIPTS/risk_check.py && echo SENT || echo BLOCKED" "BLOCKED"

run_qs "?> pass && → runs" \
    "setenv QSHELL_HOME $QSHELL_HOME
echo 'AAPL BUY 100 185.50' ?> python3 $SCRIPTS/risk_check.py && echo SENT" "SENT"

run_qs "?> fail || → fallback" \
    "setenv QSHELL_HOME $QSHELL_HOME
echo 'GME BUY 99999 20.00' ?> python3 $SCRIPTS/risk_check.py || echo FALLBACK" "FALLBACK"

section "|> CSV logging"
CSV1=$(mktemp /tmp/qshell_XXXXXX.csv)
run_qs "|> writes timestamped line" \
    "setenv QSHELL_HOME $QSHELL_HOME
echo ORDER_DATA |> $CSV1" ""; grep -qF "ORDER_DATA" "$CSV1" && ok "|> content correct" || fail "|> content missing"
grep -qF "T" "$CSV1" && ok "|> ISO timestamp present" || fail "|> timestamp missing"
rm -f "$CSV1"

section "assert + trading env"
run_qs "assert CAPITAL guard" \
    "setcapital 5000
assert \$CAPITAL > 10000 || echo BLOCKED" "BLOCKED"

run_qs "assert >= works" \
    "setcapital 100000
assert \$CAPITAL >= 25000 && echo OK" "OK"

run_qs "setmarket loads aliases" \
    "setenv QSHELL_HOME $QSHELL_HOME
setmarket NYSE
alias" "mstatus"

run_qs "rejections alias works" \
    "setenv QSHELL_HOME $QSHELL_HOME
setmarket NYSE
rejections" "REJECTED"

# ════════════════════════════════════════════════════════════════
part "PART 4 — Template Validation (18 tests)"
# ════════════════════════════════════════════════════════════════
section "Template structure checks"
for tpl in momentum_daily mean_reversion market_make backtest_runner; do
    f="$TEMPLATES/${tpl}.sh"
    grep -q "<<" "$f" && fail "$tpl: has heredocs (blocking)" || ok "$tpl: no heredocs"
done

grep -q "|>" "$TEMPLATES/momentum_daily.sh"  && ok "momentum: uses |> logging"  || fail "momentum: missing |>"
grep -q "?>" "$TEMPLATES/mean_reversion.sh"  && ok "mean_rev: uses ?> gate"     || fail "mean_rev: missing ?>"
grep -q "?>" "$TEMPLATES/market_make.sh"     && ok "market_make: uses ?> gate"  || fail "market_make: missing ?>"
grep -q "assert" "$TEMPLATES/momentum_daily.sh" && ok "momentum: has assert guard" || fail "momentum: no assert"
grep -q "assert" "$TEMPLATES/mean_reversion.sh" && ok "mean_rev: has assert guard" || fail "mean_rev: no assert"

section "Strategy assert logic (QShell)"
run_qs "momentum capital guard" \
    "setcapital 5000
assert \$CAPITAL > 10000 || echo BLOCKED" "BLOCKED"

run_qs "mean_rev capital guard" \
    "setcapital 10000
assert \$CAPITAL >= 25000 || echo CAPITAL_BLOCKED" "CAPITAL_BLOCKED"

run_qs "market_make spread guard" \
    "setenv SPREAD_BPS 1
assert \$SPREAD_BPS >= 3 || echo SPREAD_TOO_TIGHT" "SPREAD_TOO_TIGHT"

run_qs "market_make ?> rejects risky quote" \
    "setenv QSHELL_HOME $QSHELL_HOME
echo 'AAPL BUY 99999 185.50' ?> python3 $SCRIPTS/risk_check.py --max_size 500 || echo MM_REJECTED" "MM_REJECTED"

section "run_backtest.py end-to-end"
TOTAL=$((TOTAL+1))
mkdir -p "$LOGS"
QSHELL_HOME=$QSHELL_HOME LOGDIR=$LOGS PASS_THRESHOLD=2 python3 $SCRIPTS/run_backtest.py >/dev/null 2>&1
NPERIODS=$(python3 -c "
f='$LOGS/backtest_results.csv'
import os
if os.path.exists(f):
    lines=[l for l in open(f) if l.strip() and not l.startswith('PERIOD')]
    print(len(lines))
else: print(0)" 2>/dev/null)
[ "$NPERIODS" = "8" ] && { echo -e "  ${GREEN}✔${RESET} backtest: 8 periods in CSV"; PASS=$((PASS+1)); } \
    || { echo -e "  ${RED}✘${RESET} backtest: expected 8 periods, got $NPERIODS"; FAIL=$((FAIL+1)); }

TOTAL=$((TOTAL+1))
grep -q "pass_rate" "$LOGS/backtest_summary.csv" 2>/dev/null \
    && { echo -e "  ${GREEN}✔${RESET} backtest: summary has pass_rate"; PASS=$((PASS+1)); } \
    || { echo -e "  ${RED}✘${RESET} backtest: summary missing pass_rate"; FAIL=$((FAIL+1)); }

# ════════════════════════════════════════════════════════════════
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "  ${GREEN}Passed${RESET} : $PASS / $TOTAL"
echo -e "  ${RED}Failed${RESET} : $FAIL / $TOTAL"
echo ""
[ $FAIL -eq 0 ] && echo -e "${GREEN}${BOLD}✔ All $TOTAL tests passed!${RESET}" \
    || echo -e "${RED}${BOLD}✘ $FAIL test(s) failed.${RESET}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"