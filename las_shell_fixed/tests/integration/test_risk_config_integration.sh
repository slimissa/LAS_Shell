#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────
# Las_shell Phase 4.3  —  Integration Tests: Risk Limit Config
# test_risk_config_integration.sh
#
# Tests the ?> risk gate + ~/.las_shell_risk config file together,
# using the built Las_shell binary.
#
# Usage:
#   chmod +x test_risk_config_integration.sh
#   ./test_risk_config_integration.sh [path/to/las_shell]
#
# Requires: las_shell binary built from project root
# ─────────────────────────────────────────────────────────────

SHELL_BIN="${1:-./las_shell}"
PASS=0
FAIL=0
TOTAL=0

# ── Colours ──────────────────────────────────────────────────
GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

section() { printf "\n${CYAN}── %s ──${NC}\n" "$1"; }

check() {
    local name="$1" expected="$2" actual="$3"
    TOTAL=$((TOTAL + 1))
    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS + 1))
        printf "  ${GREEN}[PASS]${NC} %s\n" "$name"
    else
        FAIL=$((FAIL + 1))
        printf "  ${RED}[FAIL]${NC} %s\n" "$name"
        printf "         expected: '%s'\n" "$expected"
        printf "         actual  : '%s'\n" "$actual"
    fi
}

check_contains() {
    local name="$1" needle="$2" haystack="$3"
    TOTAL=$((TOTAL + 1))
    if echo "$haystack" | grep -q "$needle"; then
        PASS=$((PASS + 1))
        printf "  ${GREEN}[PASS]${NC} %s\n" "$name"
    else
        FAIL=$((FAIL + 1))
        printf "  ${RED}[FAIL]${NC} %s\n" "$name"
        printf "         expected to contain: '%s'\n" "$needle"
        printf "         actual output      : '%s'\n" "$haystack"
    fi
}

run() {
    # run a command through the shell, return exit code
    # usage: run "las_shell command line"
    HOME="$TEST_HOME" "$SHELL_BIN" -c "$1" 2>&1
}

run_exit() {
    HOME="$TEST_HOME" "$SHELL_BIN" -c "$1" >/dev/null 2>&1
    echo $?
}

# ── Setup: isolated HOME with our test config ─────────────────
TEST_HOME="$(mktemp -d /tmp/las_shell_integration_XXXXXX)"
RISK_CFG="$TEST_HOME/.las_shell_risk"
REJECTIONS="$TEST_HOME/.las_shell_risk_rejections"

cleanup() {
    rm -rf "$TEST_HOME"
}
trap cleanup EXIT

write_risk_cfg() {
    cat > "$RISK_CFG"
}

# ─────────────────────────────────────────────────────────────

printf "\n╭────────────────────────────────────────────────────────╮\n"
printf "│  Las_shell 4.3 Integration Tests — Risk Config + ?> Gate  │\n"
printf "╰────────────────────────────────────────────────────────╯\n"

# ── Check binary exists ───────────────────────────────────────
if [ ! -x "$SHELL_BIN" ]; then
    printf "${RED}ERROR: shell binary not found at '%s'${NC}\n" "$SHELL_BIN"
    printf "Build with: make\n"
    exit 1
fi

# ─────────────────────────────────────────────────────────────
section "1. riskconfig show — no config file"

rm -f "$RISK_CFG"
out=$(run "riskconfig show")
check_contains "show with no file reports defaults" "built-in defaults" "$out"

# ─────────────────────────────────────────────────────────────
section "2. riskconfig show — with config file"

write_risk_cfg << 'EOF'
MAX_POSITION_SIZE   = 1000
MIN_POSITION_SIZE   = 1
MAX_DRAWDOWN_PCT    = 5.0
MAX_DAILY_LOSS      = 2000
MAX_ORDER_NOTIONAL  = 500000
MAX_ORDERS_PER_DAY  = 50
ALLOWED_SYMBOLS     = SPY,QQQ,IWM,AAPL,MSFT
BLOCKED_SYMBOLS     = GME,AMC
EOF

out=$(run "riskconfig show")
check_contains "show: MAX_POSITION_SIZE = 1000" "1000" "$out"
check_contains "show: MAX_DRAWDOWN_PCT  = 5.00" "5.00" "$out"
check_contains "show: ALLOWED_SYMBOLS listed"   "SPY"  "$out"
check_contains "show: BLOCKED_SYMBOLS listed"   "GME"  "$out"

# ─────────────────────────────────────────────────────────────
section "3. riskconfig path"

out=$(run "riskconfig path")
check_contains "path prints config file location" ".las_shell_risk" "$out"

# ─────────────────────────────────────────────────────────────
section "4. riskconfig reload"

# Change config on disk, reload, verify new value appears
write_risk_cfg << 'EOF'
MAX_POSITION_SIZE = 777
MAX_DRAWDOWN_PCT  = 3.0
MAX_DAILY_LOSS    = 999
EOF

out=$(run "riskconfig reload")
check_contains "reload shows new MAX_POSITION_SIZE=777" "777" "$out"

# ─────────────────────────────────────────────────────────────
section "5. ?> gate — allowed symbol, valid size → PASS"

write_risk_cfg << 'EOF'
MAX_POSITION_SIZE  = 1000
MIN_POSITION_SIZE  = 1
MAX_ORDER_NOTIONAL = 500000
ALLOWED_SYMBOLS    = SPY,QQQ,AAPL
BLOCKED_SYMBOLS    = GME,AMC
EOF

# echo an order through the risk gate — passthrough.sh exits 0
# We use 'cat' as the right side (exit 0 = pass)
out=$(run 'echo "SPY BUY 100 485.00" ?> cat')
check_contains "SPY 100@485 passes gate and forwards to cat" "SPY" "$out"

exit_code=$(run_exit 'echo "SPY BUY 100 485.00" ?> cat')
check "$exit_code (SPY valid) = 0" "0" "$exit_code"

# ─────────────────────────────────────────────────────────────
section "6. ?> gate — blocked symbol → REJECT"

exit_code=$(run_exit 'echo "GME BUY 100 15.00" ?> cat')
check "GME (blocked) gate exit = 1" "1" "$exit_code"

out=$(run 'echo "AMC SELL 50 6.00" ?> cat' 2>&1)
check_contains "AMC rejection mentions BLOCKED" "BLOCKED" "$out"

# Verify rejection is logged
sleep 0.1  # allow file write
if [ -f "$REJECTIONS" ]; then
    check_contains "rejection logged to file" "CONFIG_LIMIT" "$(cat "$REJECTIONS")"
else
    FAIL=$((FAIL+1)); TOTAL=$((TOTAL+1))
    printf "  ${RED}[FAIL]${NC} rejection file not created\n"
fi

# ─────────────────────────────────────────────────────────────
section "7. ?> gate — symbol not in allowed list → REJECT"

exit_code=$(run_exit 'echo "TSLA BUY 100 200.00" ?> cat')
check "TSLA (not in allowed list) gate exit = 1" "1" "$exit_code"

out=$(run 'echo "NVDA BUY 50 400.00" ?> cat' 2>&1)
check_contains "NVDA rejection mentions ALLOWED" "ALLOWED" "$out"

# ─────────────────────────────────────────────────────────────
section "8. ?> gate — size over MAX_POSITION_SIZE → REJECT"

exit_code=$(run_exit 'echo "SPY BUY 1001 1.00" ?> cat')
check "SPY 1001 (over max 1000) gate exit = 1" "1" "$exit_code"

out=$(run 'echo "SPY BUY 1001 1.00" ?> cat' 2>&1)
check_contains "size rejection mentions MAX_POSITION_SIZE" "MAX_POSITION_SIZE" "$out"

# ─────────────────────────────────────────────────────────────
section "9. ?> gate — notional over limit → REJECT"

# 1000 * $510 = $510,000 > $500,000
exit_code=$(run_exit 'echo "SPY BUY 1000 510.00" ?> cat')
check "SPY 1000@510 notional $510k > $500k -> exit 1" "1" "$exit_code"

out=$(run 'echo "SPY BUY 1000 510.00" ?> cat' 2>&1)
check_contains "notional rejection mentions MAX_ORDER_NOTIONAL" "MAX_ORDER_NOTIONAL" "$out"

# ─────────────────────────────────────────────────────────────
section "10. assert (zero-arg) — drawdown within limit → PASS"

# Write a config with drawdown and daily loss limits for zero-arg assert tests
write_risk_cfg << 'EOF'
MAX_DRAWDOWN_PCT = 5.0
MAX_DAILY_LOSS   = 2000
ALLOWED_SYMBOLS  = SPY,QQQ,AAPL,MSFT
EOF

exit_code=$(run_exit 'setenv DRAWDOWN 2.5; setenv DAILY_LOSS 500; assert')
check "assert with DRAWDOWN=2.5, DAILY_LOSS=500 exits 0" "0" "$exit_code"

# ─────────────────────────────────────────────────────────────
section "11. assert (zero-arg) — drawdown over limit → FAIL"

exit_code=$(run_exit 'setenv DRAWDOWN 6.0; assert')
check "assert with DRAWDOWN=6.0 (over 5%) exits 1" "1" "$exit_code"

out=$(run 'setenv DRAWDOWN 6.0; assert' 2>&1)
check_contains "assert failure mentions MAX_DRAWDOWN_PCT" "MAX_DRAWDOWN_PCT" "$out"

# ─────────────────────────────────────────────────────────────
section "12. assert (zero-arg) — daily loss over limit → FAIL"

exit_code=$(run_exit 'setenv DAILY_LOSS 2500; assert')
check "assert with DAILY_LOSS=2500 (over 2000) exits 1" "1" "$exit_code"

# ─────────────────────────────────────────────────────────────
section "13. assert (explicit args) still works normally"

exit_code=$(run_exit 'assert 3 < 5')
check "assert 3 < 5 -> exit 0 (unchanged)" "0" "$exit_code"

exit_code=$(run_exit 'assert 6 < 5')
check "assert 6 < 5 -> exit 1 (unchanged)" "1" "$exit_code"

exit_code=$(run_exit 'assert hello == hello')
check "assert hello == hello -> exit 0" "0" "$exit_code"

# ─────────────────────────────────────────────────────────────
section "14. Full end-to-end: strategy pipeline with risk gate"

# Simulate the roadmap's end-state pipeline using shell here-docs
# universe → risk_filter via ?> gate

write_risk_cfg << 'EOF'
MAX_POSITION_SIZE  = 500
MIN_POSITION_SIZE  = 1
MAX_ORDER_NOTIONAL = 200000
ALLOWED_SYMBOLS    = SPY,QQQ,AAPL,MSFT
BLOCKED_SYMBOLS    = GME
EOF

# A valid order batch (all SPY under limits)
out=$(run 'printf "SPY BUY 100 490.00\nQQQ BUY 200 380.00\n" ?> cat')
check_contains "SPY+QQQ batch all pass gate" "SPY" "$out"
check_contains "QQQ in forwarded output" "QQQ" "$out"

# A mixed batch: one blocked, rest valid
# The gate rejects the entire batch on first violation
exit_code=$(run_exit 'printf "SPY BUY 100 490.00\nGME BUY 50 15.00\n" ?> cat')
check "mixed batch with GME blocked -> exit 1" "1" "$exit_code"

# ─────────────────────────────────────────────────────────────
printf "\n╭────────────────────────────────────────────────────────╮\n"
printf "│  Results:  %3d passed  /  %3d failed  /  %3d total     │\n" \
       "$PASS" "$FAIL" "$TOTAL"
printf "╰────────────────────────────────────────────────────────╯\n\n"

exit $FAIL
