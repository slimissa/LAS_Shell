#!/usr/bin/env las_shell
# ═══════════════════════════════════════════════════════════════
# pipeline/run_pipeline.sh
# Run from root: ./las_shell pipeline/run_pipeline.sh
# ═══════════════════════════════════════════════════════════════

setenv LAS_SHELL_HOME $(pwd)
setenv PIPELINE $LAS_SHELL_HOME/pipeline
setenv LOGDIR $LAS_SHELL_HOME/logs

mkdir -p $LOGDIR

setmarket NYSE
setcapital 100000
setaccount PAPER

echo ══════════════════════════════════════════════════════════════
echo   Las_shell Strategy Pipeline
echo ══════════════════════════════════════════════════════════════
echo Capital  : $CAPITAL
echo Account  : $ACCOUNT
echo Started  : $(date +%Y-%m-%dT%H:%M:%S)
echo ""

assert $CAPITAL > 10000 || exit 1

echo ── Stage flow ───────────────────────────────────────────────
echo   universe - momentum_filter - risk_filter - size_positions - execute
echo ""

echo ── Run 1: Full Python pipeline ──────────────────────────────
python3 $PIPELINE/universe.py \
    | python3 $PIPELINE/momentum_filter.py --threshold 0.2 --topn 5 \
    | python3 $PIPELINE/risk_filter.py \
    | python3 $PIPELINE/size_positions.py --model signal \
    | python3 $PIPELINE/execute.py --mode paper > $LOGDIR/pipeline_executions.csv

echo ""
echo ── Execution receipts ───────────────────────────────────────
cat $LOGDIR/pipeline_executions.csv

echo ""
echo ── Run 2: CSV audit logging with |> ─────────────────────────
python3 $PIPELINE/universe.py \
    | python3 $PIPELINE/momentum_filter.py --threshold 0.2 --topn 3 \
    | python3 $PIPELINE/risk_filter.py \
    | python3 $PIPELINE/size_positions.py --model equal \
    | python3 $PIPELINE/execute.py --mode paper --dry_run |> $LOGDIR/pipeline_audit.csv

echo ""
echo ── Run 3: Mixed C + Python pipeline ─────────────────────────
$PIPELINE/universe_c \
    | python3 $PIPELINE/momentum_filter.py --threshold 0.15 \
    | $PIPELINE/bin/risk_filter \
    | python3 $PIPELINE/size_positions.py --model equal \
    | python3 $PIPELINE/execute.py --mode paper --dry_run

echo ""
echo Pipeline complete: $(date +%Y-%m-%dT%H:%M:%S)
echo ══════════════════════════════════════════════════════════════