#!/usr/bin/las_shell
# test_streaming_sub.sh
# ─────────────────────────────────────────────────────────────────────────
# Phase 3.1 — $<() Streaming Substitution Test Suite
#
# Tests:
#   T1  One-shot:  price=$<(seq 1 5)  → reads first line "1"
#   T2  Finite stream:  while x=$<(seq 1 5); do echo $x; done  → prints 1..5
#   T3  Roadmap example: streaming price feed with assert guard
#   T4  Cleanup:  FIFO + child process reaped after loop
# ─────────────────────────────────────────────────────────────────────────

setenv LAS_SHELL_HOME $(pwd)

echo "=============================================="
echo " Las_shell Phase 3.1 — Streaming Substitution"
echo "=============================================="

# ── T1: One-shot $<() ────────────────────────────────────────────────────
echo ""
echo "T1: One-shot  price=\$<(echo 42)"
setenv PRICE_ONESHOT 0
setenv PRICE_ONESHOT $<(echo 42)
echo "    PRICE_ONESHOT = $PRICE_ONESHOT"
assert $PRICE_ONESHOT == 42
echo "    [PASS] one-shot assignment"

# ── T2: Finite stream loop ────────────────────────────────────────────────
echo ""
echo "T2: Finite stream — while x=\$<(seq 1 5); do echo x; done"
setenv STREAM_COUNT 0
while x=$<(seq 1 5)
do
    echo "    got: $x"
    setenv STREAM_COUNT $(expr $STREAM_COUNT + 1)
done
echo "    iterations = $STREAM_COUNT"
assert $STREAM_COUNT == 5
echo "    [PASS] finite stream — 5 lines received"

# ── T3: Roadmap example (3 ticks from a price feed) ─────────────────────
echo ""
echo "T3: Price feed stream — 3 ticks from quote AAPL"
setenv TICK_COUNT 0
while price=$<(python3 $LAS_SHELL_HOME/scripts/quote.py --stream 3 AAPL)
do
    echo "    AAPL tick: \$price = $price"
    assert $price > 0
    setenv TICK_COUNT $(expr $TICK_COUNT + 1)
done
echo "    ticks received = $TICK_COUNT"
assert $TICK_COUNT == 3
echo "    [PASS] price feed streaming — 3 ticks, all > 0"

# ── T4: Multiple $<() in one script ──────────────────────────────────────
echo ""
echo "T4: Two streams in same script"
setenv A 0
setenv B 0
setenv A $<(echo hello)
setenv B $<(echo world)
echo "    A=$A  B=$B"
assert $A == hello
assert $B == world
echo "    [PASS] two independent one-shot streams"

# ── T5: Nested expansion — $<() result used in $(expr) ──────────────────
echo ""
echo "T5: Nested — \$(expr \$<(echo 10) + 5)"
setenv RESULT 0
setenv RAW $<(echo 10)
setenv RESULT $(expr $RAW + 5)
echo "    RAW=$RAW  RESULT=$RESULT"
assert $RESULT == 15
echo "    [PASS] $<() result used in arithmetic"

echo ""
echo "=============================================="
echo " All streaming substitution tests PASSED"
echo "=============================================="
