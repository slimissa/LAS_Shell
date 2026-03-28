#!/usr/bin/env las_shell
# ═══════════════════════════════════════════════════════════════
# pipeline/run_pipeline.sh
# Run from DIY_Shell root: ./las_shell pipeline/run_pipeline.sh
# ═══════════════════════════════════════════════════════════════

# Fix 1: set vars WITHOUT double quotes so expand_vars_in_line works
setenv QSHELL_HOME /home/slim/Documents/DIY_Shell
setenv PIPELINE $QSHELL_HOME/pipeline
setenv LOGDIR $QSHELL_HOME/logs

# Fix 2: mkdir via external command — gets path from QShell internal env
mkdir -p $LOGDIR

setmarket NYSE
setcapital 100000
setaccount PAPER

echo ══════════════════════════════════════════════════════════════
echo   QShell Strategy Pipeline
echo ══════════════════════════════════════════════════════════════
# Fix 3: no double quotes around $VAR
echo Capital  : $CAPITAL
echo Account  : $ACCOUNT
echo Started  : $(date +%Y-%m-%dT%H:%M:%S)
echo ""

assert $CAPITAL > 10000 || exit 1

echo ── Stage flow ───────────────────────────────────────────────
echo   universe - momentum_filter - risk_filter - size_positions - execute
echo ""

# Fix 4: entire pipeline on ONE line — QShell does not support backslash continuation
echo ── Running full Python pipeline ─────────────────────────────
python3 $PIPELINE/universe.py | python3 $PIPELINE/momentum_filter.py --threshold 0.2 --topn 5 | python3 $PIPELINE/risk_filter.py | python3 $PIPELINE/size_positions.py --model signal | python3 $PIPELINE/execute.py --mode paper > $LOGDIR/pipeline_executions.csv

echo ""
echo ── Execution receipts ───────────────────────────────────────
cat $LOGDIR/pipeline_executions.csv

echo ""
echo ── Logging receipts with timestamp via |> ───────────────────
python3 $PIPELINE/universe.py | python3 $PIPELINE/momentum_filter.py --threshold 0.2 --topn 3 | python3 $PIPELINE/risk_filter.py | python3 $PIPELINE/size_positions.py --model equal | python3 $PIPELINE/execute.py --mode paper --dry_run |> $LOGDIR/pipeline_audit.csv

echo ""
echo ── Mixed C + Python pipeline ────────────────────────────────
$PIPELINE/universe | python3 $PIPELINE/momentum_filter.py --threshold 0.15 | $PIPELINE/risk_filter | python3 $PIPELINE/size_positions.py --model equal | python3 $PIPELINE/execute.py --mode paper --dry_run

echo ""
echo Pipeline complete: $(date +%Y-%m-%dT%H:%M:%S)
echo ══════════════════════════════════════════════════════════════