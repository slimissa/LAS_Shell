#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# test_market_wait_operators.sh — Las_shell v0.6.0 Milestone 3 Phase 6
#
# Dedicated coverage for @next_open / @before_close, closing the same kind
# of gap test_multi_currency_pnl.sh closed for Phase 7: this was previously
# verified only manually in-session (FIFO + kill -INT/-TERM), not committed
# as a repeatable check.
#
# Two things worth reading before touching this file:
#
# 1. "Near-future wait" tests don't wait for a REAL market boundary --
#    that would make the suite's runtime depend on the time of day it
#    happens to run. Instead they compute the real next-close/next-open
#    time via `calendar next-close/next-open`, then solve for the MINUTES
#    argument that puts the *target* a few seconds from now:
#        target = close_epoch - minutes*60  =>  minutes = (close_epoch - now - N) / 60
#    This exercises the real calendar math and the real wait loop, just
#    with a short, deterministic wait regardless of what time it is.
#
# 2. Test 3 ("@next_open with a past target") and test 7 ("unknown
#    exchange -> error") are written against the REAL observed behavior,
#    not an idealized one -- see the comments at each test for what was
#    found and why the test asserts what it does.
# ═══════════════════════════════════════════════════════════════════════════
set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$PROJECT_ROOT"
export LAS_SHELL_HOME="$PROJECT_ROOT"

SHELL_BIN="$PROJECT_ROOT/las_shell"
PASS=0
FAIL=0

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; NC='\033[0m'

assert_contains() {
    local label="$1" haystack="$2" needle="$3"
    if [[ "$haystack" == *"$needle"* ]]; then
        printf "  ${GREEN}PASS${NC}  %s\n" "$label"; PASS=$((PASS+1))
    else
        printf "  ${RED}FAIL${NC}  %s\n" "$label"
        printf "         Expected to contain: %s\n" "$needle"
        printf "         Got: %s\n" "$(echo "$haystack" | tr '\n' '|')"
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
# Interactive-mode output echoes the input line itself (readline prompt
# echo), so a command like "echo SHOULD_NOT_RUN_X" appearing in output
# is ambiguous: once for the echoed input line, twice if the command
# actually also ran and printed its own output. assert_not_contains is
# too broad for these cases -- use an occurrence count instead: exactly
# 1 occurrence (input echo only) means the command did NOT run.
assert_did_not_run() {
    local label="$1" haystack="$2" marker="$3"
    local count
    count=$(grep -o -- "$marker" <<<"$haystack" | wc -l)
    if (( count <= 1 )); then
        printf "  ${GREEN}PASS${NC}  %s (occurrences: %d)\n" "$label" "$count"; PASS=$((PASS+1))
    else
        printf "  ${RED}FAIL${NC}  %s\n" "$label"
        printf "         marker appeared %d times -- expected <=1 (input-line echo only)\n" "$count"
        FAIL=$((FAIL+1))
    fi
}

# ── Compute MINUTES for @before_close / target for @next_open such that
#    the target lands roughly `offset_sec` seconds from now, regardless
#    of real market hours. Note: MINUTES is integer-only, so the
#    achievable precision is inherently ±30s (rounding to the nearest
#    minute) -- round to nearest here (not floor) to keep the actual
#    wait centered on offset_sec instead of skewed up to 59s longer.
minutes_for_close_target() {
    local exchange="$1" offset_sec="$2"
    local close_str close_epoch now_epoch diff
    close_str=$($SHELL_BIN -c "calendar next-close $exchange" 2>/dev/null | tail -1)
    close_epoch=$(date -u -d "$close_str" +%s 2>/dev/null) || { echo ""; return; }
    now_epoch=$(date -u +%s)
    diff=$((close_epoch - now_epoch))
    if (( diff <= offset_sec )); then echo ""; return; fi
    awk -v d="$diff" -v o="$offset_sec" 'BEGIN{printf "%d", (d-o)/60 + 0.5}'
}

FIFO_DIR=""
cleanup() {
    [[ -n "$FIFO_DIR" && -d "$FIFO_DIR" ]] && rm -rf "$FIFO_DIR"
    pkill -9 -f "las_shell.*test_market_wait" 2>/dev/null || true
}
trap cleanup EXIT

echo "══ Milestone 3 Phase 6: @next_open / @before_close ═══════════════"

# ── 1. @before_close, target already in the past -> runs immediately ──
echo ""
echo "── 1. @before_close: past target runs immediately ──"
START=$(date +%s%N)
OUT=$($SHELL_BIN -c "@before_close NYSE 999999 echo IMMEDIATE_RUN_1" 2>&1)
ELAPSED_MS=$(( ($(date +%s%N) - START) / 1000000 ))
assert_contains "command output present"          "$OUT" "IMMEDIATE_RUN_1"
assert_not_contains "no 'waiting until' message"   "$OUT" "waiting until"
if (( ELAPSED_MS < 2000 )); then
    printf "  ${GREEN}PASS${NC}  completed fast (%dms, no real wait)\n" "$ELAPSED_MS"; PASS=$((PASS+1))
else
    printf "  ${RED}FAIL${NC}  took %dms -- expected near-instant\n" "$ELAPSED_MS"; FAIL=$((FAIL+1))
fi

# ── 2. @before_close, near-future target -> waits, then runs ──
echo ""
echo "── 2. @before_close: near-future target waits then runs ──"
MINUTES=$(minutes_for_close_target "NYSE" 30)
if [[ -z "$MINUTES" ]]; then
    printf "  ${YELLOW}SKIP${NC}  NYSE close too near/unavailable to compute a safe test window\n"
else
    START=$(date +%s)
    OUT=$(timeout 90 $SHELL_BIN -c "@before_close NYSE $MINUTES echo WAITED_THEN_RAN" 2>&1)
    ELAPSED=$(( $(date +%s) - START ))
    assert_contains "shows 'waiting until' message"     "$OUT" "waiting until"
    assert_contains "command ran after the wait"        "$OUT" "WAITED_THEN_RAN"
    if (( ELAPSED >= 1 && ELAPSED <= 65 )); then
        printf "  ${GREEN}PASS${NC}  waited a real, bounded amount of time (%ds)\n" "$ELAPSED"; PASS=$((PASS+1))
    else
        printf "  ${RED}FAIL${NC}  elapsed %ds -- expected roughly 1-65s (MINUTES has "\
"±30s rounding granularity around a 30s target)\n" "$ELAPSED"; FAIL=$((FAIL+1))
    fi
fi

# ── 3. @next_open — see file header note: calendar_next_open_time() is
#    defined to always return "at or after now", so a literal "past
#    target" is not reachable through real data (unlike @before_close,
#    which can via a large MINUTES value -- @next_open has no MINUTES
#    argument to exploit the same way). This tests the nearest honest
#    equivalent: a short real wait that reaches its target and runs,
#    proving the same wait_until_epoch completion path @before_close
#    uses. Uses XASX as a second, independent exchange/timezone from
#    test 2's NYSE. ──
echo ""
echo "── 3. @next_open: short real wait reaches target and runs ──"
echo "    (note: a literal 'past target' isn't reachable for @next_open --"
echo "     calendar_next_open_time() always returns >= now by contract;"
echo "     this instead verifies the same completion path via a short wait)"
OPEN_STR=$($SHELL_BIN -c "calendar next-open XASX" 2>/dev/null | tail -1)
OPEN_EPOCH=$(date -u -d "$OPEN_STR" +%s 2>/dev/null || echo "")
NOW_EPOCH=$(date -u +%s)
if [[ -z "$OPEN_EPOCH" ]] || (( OPEN_EPOCH - NOW_EPOCH > 20 )); then
    printf "  ${YELLOW}SKIP${NC}  XASX next open is not within the next 20s right now "
    printf "(next open: %s) -- @next_open has no MINUTES-style argument to\n" "$OPEN_STR"
    printf "         force a short target, so this test only runs when the real\n"
    printf "         boundary happens to be imminent. Completion mechanics are\n"
    printf "         still covered indirectly: wait_until_epoch() is the exact\n"
    printf "         same function test 2 already exercises for @before_close.\n"
else
    OUT=$(timeout 30 $SHELL_BIN -c "@next_open XASX echo NEXT_OPEN_RAN" 2>&1)
    assert_contains "@next_open command ran after reaching the open" "$OUT" "NEXT_OPEN_RAN"
fi

# ── 4. SIGINT during a long wait -> cancels cleanly, command does NOT run ──
echo ""
echo "── 4. SIGINT cancels a long wait without running the command ──"
FIFO_DIR=$(mktemp -d)
mkfifo "$FIFO_DIR/in"
setsid $SHELL_BIN < "$FIFO_DIR/in" > "$FIFO_DIR/out" 2>&1 &
SHELL_PID=$!
exec 5>"$FIFO_DIR/in"
echo '@next_open XTKS echo SHOULD_NOT_RUN_SIGINT' >&5
sleep 2
kill -INT "$SHELL_PID" 2>/dev/null
sleep 1
STILL_ALIVE=0; kill -0 "$SHELL_PID" 2>/dev/null && STILL_ALIVE=1
echo 'echo PROOF_OF_LIFE_AFTER_SIGINT' >&5
sleep 1
exec 5>&-
kill -9 "$SHELL_PID" 2>/dev/null; wait "$SHELL_PID" 2>/dev/null
OUT=$(cat "$FIFO_DIR/out")
assert_eq          "shell survives SIGINT (not killed)"     "$STILL_ALIVE" "1"
assert_contains     "prints cancellation message"            "$OUT" "cancelled"
assert_did_not_run  "pending command did NOT run"             "$OUT" "SHOULD_NOT_RUN_SIGINT"
assert_contains     "shell keeps processing after cancel"    "$OUT" "PROOF_OF_LIFE_AFTER_SIGINT"
rm -rf "$FIFO_DIR"; FIFO_DIR=""

# ── 5. SIGTERM during a long wait -> clean shutdown, command does NOT run ──
echo ""
echo "── 5. SIGTERM triggers clean shutdown without running the command ──"
FIFO_DIR=$(mktemp -d)
mkfifo "$FIFO_DIR/in"
setsid $SHELL_BIN < "$FIFO_DIR/in" > "$FIFO_DIR/out" 2>&1 &
SHELL_PID=$!
exec 6>"$FIFO_DIR/in"
echo '@next_open XTKS echo SHOULD_NOT_RUN_SIGTERM' >&6
sleep 2
kill -TERM "$SHELL_PID" 2>/dev/null
sleep 2
STILL_ALIVE=0; kill -0 "$SHELL_PID" 2>/dev/null && STILL_ALIVE=1
exec 6>&-
wait "$SHELL_PID" 2>/dev/null
OUT=$(cat "$FIFO_DIR/out")
assert_eq          "process exits after SIGTERM (not still running)" "$STILL_ALIVE" "0"
assert_contains     "checkpoint save-and-exit message shown"          "$OUT" "SIGTERM received"
assert_did_not_run  "pending command did NOT run"                     "$OUT" "SHOULD_NOT_RUN_SIGTERM"
rm -rf "$FIFO_DIR"; FIFO_DIR=""

# ── 6. Exchange alias resolution ──
echo ""
echo "── 6. Exchange alias resolution (NYSE→XNYS, LSE→XLON) ──"
OUT_ALIAS=$($SHELL_BIN -c "@before_close NYSE 999999 echo ALIAS_NYSE" 2>&1)
OUT_MIC=$($SHELL_BIN -c "@before_close XNYS 999999 echo ALIAS_NYSE" 2>&1)
assert_contains "NYSE alias runs the command" "$OUT_ALIAS" "ALIAS_NYSE"
assert_contains "XNYS (MIC) runs the command"  "$OUT_MIC"   "ALIAS_NYSE"

OUT_LSE=$($SHELL_BIN -c "@before_close LSE 999999 echo ALIAS_LSE" 2>&1)
OUT_XLON=$($SHELL_BIN -c "@before_close XLON 999999 echo ALIAS_LSE" 2>&1)
assert_contains "LSE alias runs the command"   "$OUT_LSE"  "ALIAS_LSE"
assert_contains "XLON (MIC) runs the command"  "$OUT_XLON" "ALIAS_LSE"

# lowercase should also resolve (calendar.c uppercases before lookup)
OUT_LOWER=$($SHELL_BIN -c "@before_close nyse 999999 echo ALIAS_LOWERCASE" 2>&1)
assert_contains "lowercase 'nyse' alias resolves" "$OUT_LOWER" "ALIAS_LOWERCASE"

# ── 7. Unknown exchange. See file header: @before_close and @next_open
#    do NOT behave the same way here -- documenting the real, differing
#    behavior rather than asserting an idealized shared one.
echo ""
echo "── 7. Unknown exchange (documenting REAL, asymmetric behavior) ──"
OUT=$($SHELL_BIN -c "@before_close FAKEEXCHANGE 30 echo SHOULD_NOT_RUN_7" 2>&1)
RC=$?
assert_eq           "@before_close FAKEEXCHANGE exits 1"     "$RC" "1"
assert_not_contains "@before_close: pending command did not run" "$OUT" "SHOULD_NOT_RUN_7"
assert_contains     "@before_close: fails on MINUTES, not a dedicated "\
"'unknown exchange' message -- FAKEEXCHANGE isn't consumed as an "\
"exchange (not resolvable), so it falls through and is parsed as the "\
"MINUTES token instead, which correctly fails as non-numeric" \
                     "$OUT" "invalid MINUTES 'FAKEEXCHANGE'"

echo "    KNOWN GAP (not fixed here, flagging only): @next_open has no"
echo "    MINUTES argument, so an unresolvable exchange token is NOT"
echo "    caught at all -- it silently falls back to \$MARKET/NYSE and"
echo "    the unresolved token becomes the START OF THE COMMAND, which"
echo "    only surfaces as a problem after potentially waiting hours for"
echo "    the (wrong, silently-defaulted) exchange. Verified below with"
echo "    a bounded-time probe, NOT a full wait -- confirming the process"
echo "    is genuinely blocked waiting (not erroring), then killed."
FIFO_DIR=$(mktemp -d)
mkfifo "$FIFO_DIR/in"
setsid $SHELL_BIN < "$FIFO_DIR/in" > "$FIFO_DIR/out" 2>&1 &
SHELL_PID=$!
exec 7>"$FIFO_DIR/in"
echo '@next_open FAKEEXCHANGE echo SHOULD_NOT_RUN_EITHER' >&7
sleep 2
OUT=$(cat "$FIFO_DIR/out")
STILL_WAITING=0; kill -0 "$SHELL_PID" 2>/dev/null && STILL_WAITING=1
assert_eq       "confirmed: silently still blocked (not an immediate error)" "$STILL_WAITING" "1"
assert_contains "confirmed: silently defaulted to NYSE, no warning shown"    "$OUT" "waiting until"
exec 7>&-
kill -9 "$SHELL_PID" 2>/dev/null; wait "$SHELL_PID" 2>/dev/null
rm -rf "$FIFO_DIR"; FIFO_DIR=""

# ── 8. Invalid MINUTES argument -> error, exit 1, command does not run ──
echo ""
echo "── 8. Invalid MINUTES argument ──"
OUT=$($SHELL_BIN -c "@before_close NYSE notanumber echo SHOULD_NOT_RUN_8" 2>&1)
RC=$?
assert_eq           "exits 1 for non-numeric MINUTES"     "$RC" "1"
assert_contains     "prints invalid-MINUTES error"        "$OUT" "invalid MINUTES 'notanumber'"
assert_not_contains "pending command did not run"         "$OUT" "SHOULD_NOT_RUN_8"

OUT=$($SHELL_BIN -c "@before_close NYSE -5 echo SHOULD_NOT_RUN_8B" 2>&1)
RC=$?
assert_eq           "exits 1 for negative MINUTES"        "$RC" "1"
assert_not_contains "pending command did not run (negative)" "$OUT" "SHOULD_NOT_RUN_8B"

OUT=$($SHELL_BIN -c "@before_close NYSE" 2>&1)
RC=$?
assert_eq           "exits 1 for missing MINUTES entirely" "$RC" "1"
assert_contains     "prints missing-argument error"        "$OUT" "missing MINUTES argument"

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
