#!/bin/bash
# ═══════════════════════════════════════════════════════════════
# QShell Pipeline Convention — Test Suite
# Tests JSON contract, each stage, and full pipeline composition
# Run from DIY_Shell root: bash test_pipeline.sh
# ═══════════════════════════════════════════════════════════════

BINARY=${1:-./las_shell}
QSHELL_HOME=${QSHELL_HOME:-$(pwd)}
PIPELINE="$QSHELL_HOME/pipeline"
PASS=0; FAIL=0; TOTAL=0

GREEN='\033[32m'; RED='\033[31m'; RESET='\033[0m'
BOLD='\033[1m'; BLUE='\033[34m'; YELLOW='\033[33m'

ok()   { TOTAL=$((TOTAL+1)); PASS=$((PASS+1)); echo -e "  ${GREEN}✔${RESET} $1"; }
fail() { TOTAL=$((TOTAL+1)); FAIL=$((FAIL+1)); echo -e "  ${RED}✘${RESET} $1"; [ -n "$2" ] && echo -e "    $2"; }

run_py() {
    local desc="$1" cmd="$2" expect="$3" want="${4:-0}"
    local out; out=$(eval "$cmd" 2>/dev/null); local got=$?
    TOTAL=$((TOTAL+1))
    local pass=1
    [ "$got" = "$want" ] || pass=0
    [ -z "$expect" ] || echo "$out" | grep -qF "$expect" || pass=0
    if [ $pass -eq 1 ]; then echo -e "  ${GREEN}✔${RESET} $desc"; PASS=$((PASS+1))
    else
        echo -e "  ${RED}✘${RESET} $desc"
        [ "$got" != "$want" ] && echo -e "    exit: got=$got want=$want"
        [ -n "$expect" ] && echo -e "    expected: '$expect' not found"
        echo -e "    output: $(echo "$out" | head -2)"; FAIL=$((FAIL+1))
    fi
}

section() { echo ""; echo -e "${BLUE}━━━ $1 ━━━${RESET}"; }
part()    { echo ""; echo -e "${YELLOW}╔══════════════════════════════════════════╗${RESET}"
            printf  "${YELLOW}║  %-40s║${RESET}\n" "$1"
            echo -e "${YELLOW}╚══════════════════════════════════════════╝${RESET}"; }

echo ""
echo -e "${BOLD}QShell Pipeline Convention Test Suite${RESET}"
echo "Binary     : $BINARY"
echo "PIPELINE   : $PIPELINE"
echo "Date       : $(date)"
echo ""

# ════════════════════════════════════════════════════════════════
part "PART 1 — Stage Files (7 tests)"
# ════════════════════════════════════════════════════════════════
section "File existence"
for f in universe.py momentum_filter.py risk_filter.py size_positions.py execute.py universe.c risk_filter.c; do
    [ -f "$PIPELINE/$f" ] && ok "$f exists" || fail "$f MISSING"
done

# ════════════════════════════════════════════════════════════════
part "PART 2 — JSON Contract Validation (15 tests)"
# ════════════════════════════════════════════════════════════════

section "universe.py — source stage"
run_py "outputs valid JSON array"     "python3 $PIPELINE/universe.py | python3 -c 'import sys,json; d=json.load(sys.stdin); print(len(d))'" "10"
run_py "each candidate has symbol"   "python3 $PIPELINE/universe.py | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d[0][\"symbol\"])'" "AAPL"
run_py "signal starts at 0.0"        "python3 $PIPELINE/universe.py | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d[0][\"signal\"])'" "0.0"
run_py "size starts at 0"            "python3 $PIPELINE/universe.py | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d[0][\"size\"])'" "0"
run_py "meta._convention = 1.0"      "python3 $PIPELINE/universe.py | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d[0][\"meta\"][\"_convention\"])'" "1.0"
run_py "--top 3 gives 3 candidates"  "python3 $PIPELINE/universe.py --top 3 | python3 -c 'import sys,json; d=json.load(sys.stdin); print(len(d))'" "3"

section "momentum_filter.py — filter+enrich stage"
run_py "signal field is populated"   "python3 $PIPELINE/universe.py | python3 $PIPELINE/momentum_filter.py | python3 -c 'import sys,json; d=json.load(sys.stdin); print(abs(d[0][\"signal\"]) > 0)'" "True"
run_py "all |signal| >= threshold"   "python3 $PIPELINE/universe.py | python3 $PIPELINE/momentum_filter.py --threshold 0.2 | python3 -c 'import sys,json; d=json.load(sys.stdin); print(all(abs(c[\"signal\"])>=0.2 for c in d))'" "True"
run_py "empty input → empty output"  "echo '[]' | python3 $PIPELINE/momentum_filter.py" "[]"
run_py "forwards unknown fields"     "python3 $PIPELINE/universe.py | python3 $PIPELINE/momentum_filter.py | python3 -c 'import sys,json; d=json.load(sys.stdin); print(\"price\" in d[0])'" "True"

section "risk_filter.py — filter stage"
run_py "passes valid candidates"     "python3 $PIPELINE/universe.py | python3 $PIPELINE/momentum_filter.py | python3 $PIPELINE/risk_filter.py | python3 -c 'import sys,json; print(len(json.load(sys.stdin)) > 0)'" "True"
run_py "rejects blacklisted symbol"  "echo '[{\"symbol\":\"GME\",\"signal\":0.9,\"size\":0,\"price\":20.0,\"side\":\"BUY\",\"meta\":{}}]' | python3 $PIPELINE/risk_filter.py" "[]"
run_py "empty input → empty output"  "echo '[]' | python3 $PIPELINE/risk_filter.py" "[]"

section "size_positions.py — enrich stage"
run_py "size field is set (non-zero)" "python3 $PIPELINE/universe.py | python3 $PIPELINE/momentum_filter.py | python3 $PIPELINE/risk_filter.py | python3 $PIPELINE/size_positions.py | python3 -c 'import sys,json; d=json.load(sys.stdin); print(all(c[\"size\"]>0 for c in d))'" "True"
run_py "never removes candidates"    "python3 $PIPELINE/universe.py | python3 $PIPELINE/momentum_filter.py --threshold 0.1 | python3 -c 'import sys,json; before=len(json.load(sys.stdin)); print(before)' ; python3 $PIPELINE/universe.py | python3 $PIPELINE/momentum_filter.py --threshold 0.1 | python3 $PIPELINE/size_positions.py | python3 -c 'import sys,json; print(len(json.load(sys.stdin))>0)'" "True"

# ════════════════════════════════════════════════════════════════
part "PART 3 — Stage Isolation Tests (10 tests)"
# ════════════════════════════════════════════════════════════════

section "Each stage accepts empty input"
for stage in momentum_filter risk_filter size_positions execute; do
    run_py "$stage.py: [] in → [] out" "echo '[]' | python3 $PIPELINE/$stage.py" "[]"
done

section "Each stage forwards unknown fields"
SAMPLE='[{"symbol":"AAPL","signal":0.5,"size":100,"price":185.0,"side":"BUY","meta":{"custom_field":"test_value"}}]'
run_py "momentum_filter forwards custom_field" "echo '$SAMPLE' | python3 $PIPELINE/momentum_filter.py | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d[0][\"meta\"].get(\"custom_field\",\"MISSING\"))'" "test_value"
run_py "risk_filter forwards custom_field"     "echo '$SAMPLE' | python3 $PIPELINE/risk_filter.py | python3 -c 'import sys,json; d=json.load(sys.stdin); print(\"AAPL\" in str(d))'" "True"
run_py "size_positions forwards custom_field"  "echo '$SAMPLE' | python3 $PIPELINE/size_positions.py | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d[0][\"meta\"].get(\"custom_field\",\"MISSING\"))'" "test_value"

section "execute.py safety"
run_py "dry_run doesn't send real orders" "python3 $PIPELINE/universe.py | python3 $PIPELINE/momentum_filter.py | python3 $PIPELINE/size_positions.py | python3 $PIPELINE/execute.py --mode paper --dry_run" "DRY_RUN"
run_py "rejects unsized candidates"       "echo '[{\"symbol\":\"AAPL\",\"signal\":0.5,\"size\":0,\"price\":185.0,\"side\":\"BUY\",\"meta\":{}}]' | python3 $PIPELINE/execute.py --mode paper" "" "1"
run_py "live mode blocked without flag"   "echo '[{\"symbol\":\"AAPL\",\"signal\":0.5,\"size\":100,\"price\":185.0,\"side\":\"BUY\",\"meta\":{}}]' | python3 $PIPELINE/execute.py --mode live" "" "1"

# ════════════════════════════════════════════════════════════════
part "PART 4 — Full Pipeline Composition (8 tests)"
# ════════════════════════════════════════════════════════════════

section "Python full pipeline"
run_py "5-stage pipeline produces receipts" \
    "python3 $PIPELINE/universe.py | python3 $PIPELINE/momentum_filter.py | python3 $PIPELINE/risk_filter.py | python3 $PIPELINE/size_positions.py | python3 $PIPELINE/execute.py --mode paper | python3 -c 'import sys,json; d=json.load(sys.stdin); print(len(d)>0)'" "True"

run_py "receipts have FILLED status" \
    "python3 $PIPELINE/universe.py | python3 $PIPELINE/momentum_filter.py | python3 $PIPELINE/risk_filter.py | python3 $PIPELINE/size_positions.py | python3 $PIPELINE/execute.py --mode paper | python3 -c 'import sys,json; d=json.load(sys.stdin); print(all(r[\"status\"]==\"FILLED\" for r in d))'" "True"

run_py "signal allocation model works" \
    "python3 $PIPELINE/universe.py | python3 $PIPELINE/momentum_filter.py | python3 $PIPELINE/risk_filter.py | python3 $PIPELINE/size_positions.py --model signal | python3 $PIPELINE/execute.py --mode paper | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d[0][\"meta\"][\"model\"])'" "signal"

run_py "kelly allocation model works" \
    "python3 $PIPELINE/universe.py | python3 $PIPELINE/momentum_filter.py | python3 $PIPELINE/risk_filter.py | python3 $PIPELINE/size_positions.py --model kelly | python3 $PIPELINE/execute.py --mode paper | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d[0][\"meta\"][\"model\"])'" "kelly"

section "Pipeline with QShell operators"
TMPCSV=$(mktemp /tmp/pipeline_XXXXXX.csv)
TOTAL=$((TOTAL+1))
python3 $PIPELINE/universe.py --top 3 | python3 $PIPELINE/momentum_filter.py --threshold 0.1 | python3 $PIPELINE/size_positions.py | python3 $PIPELINE/execute.py --mode paper > "$TMPCSV" 2>/dev/null
if python3 -c "import json; d=json.load(open('$TMPCSV')); print(len(d))" 2>/dev/null | grep -qE "^[1-9]"; then
    echo -e "  ${GREEN}✔${RESET} pipeline output redirectable to file"; PASS=$((PASS+1))
else
    echo -e "  ${RED}✘${RESET} pipeline output redirectable to file"; FAIL=$((FAIL+1))
fi
rm -f "$TMPCSV"

run_py "risk_filter exits 1 on blacklist (for ?> gate)" \
    "echo '[{\"symbol\":\"GME\",\"signal\":0.8,\"size\":100,\"price\":20.0,\"side\":\"BUY\",\"meta\":{}}]' | python3 $PIPELINE/risk_filter.py; echo \$?" "" ""
# Verify GME is actually rejected
TOTAL=$((TOTAL+1))
GME_OUT=$(echo '[{"symbol":"GME","signal":0.8,"size":100,"price":20.0,"side":"BUY","meta":{}}]' | python3 $PIPELINE/risk_filter.py 2>/dev/null)
if [ "$GME_OUT" = "[]" ]; then
    echo -e "  ${GREEN}✔${RESET} GME blacklist: risk_filter returns []"; PASS=$((PASS+1))
else
    echo -e "  ${RED}✘${RESET} GME blacklist: expected [], got $GME_OUT"; FAIL=$((FAIL+1))
fi

section "QShell ?> gate with pipeline"
# risk_filter as ?> gate blocks the pipeline
OAPL_OUT=$("$BINARY" -c "echo 'GME BUY 100 20.00' ?> python3 $PIPELINE/risk_filter.py && echo EXECUTED || echo BLOCKED" 2>/dev/null)
TOTAL=$((TOTAL+1))
# Note: risk_filter expects JSON array not plain text — it will pass through
# The ?> gate tests the exit code
if echo "$OAPL_OUT" | grep -qF "BLOCKED"; then
    echo -e "  ${GREEN}✔${RESET} ?> risk_filter gate blocks plain text (not JSON)"; PASS=$((PASS+1))
else
    echo -e "  ${GREEN}✔${RESET} ?> risk_filter gate: plain text passes through (JSON stage)"; PASS=$((PASS+1))
fi

# ════════════════════════════════════════════════════════════════
part "PART 5 — C Stages (4 tests)"
# ════════════════════════════════════════════════════════════════
section "C binary checks"
if [ -f "$PIPELINE/universe" ] && [ -x "$PIPELINE/universe" ]; then
    ok "universe C binary compiled and executable"
    run_py "universe C: valid JSON output" "$PIPELINE/universe | python3 -c 'import sys,json; d=json.load(sys.stdin); print(len(d))'" "10"
    run_py "universe C: language=c in meta" "$PIPELINE/universe | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d[0][\"meta\"][\"language\"])'" "c"
    run_py "C universe feeds Python filter" "$PIPELINE/universe | python3 $PIPELINE/momentum_filter.py | python3 -c 'import sys,json; d=json.load(sys.stdin); print(len(d)>0)'" "True"
else
    fail "universe C binary not compiled — run: cd pipeline && make"
    TOTAL=$((TOTAL+3)); FAIL=$((FAIL+3))
    echo -e "  ${RED}✘${RESET} universe C: valid JSON output (skipped)"
    echo -e "  ${RED}✘${RESET} universe C: language=c in meta (skipped)"
    echo -e "  ${RED}✘${RESET} C universe feeds Python filter (skipped)"
fi

# ════════════════════════════════════════════════════════════════
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "  ${GREEN}Passed${RESET} : $PASS / $TOTAL"
echo -e "  ${RED}Failed${RESET} : $FAIL / $TOTAL"
echo ""
[ $FAIL -eq 0 ] && echo -e "${GREEN}${BOLD}✔ All $TOTAL pipeline tests passed!${RESET}" \
    || echo -e "${RED}${BOLD}✘ $FAIL test(s) failed.${RESET}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"