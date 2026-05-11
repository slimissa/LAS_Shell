#!/usr/bin/env las_shell
# ═══════════════════════════════════════════════════════════════
# templates/momentum.sh — Intraday Momentum Strategy
# The full end-state strategy from the Las_shell roadmap.
#
# Usage:
#   ./las_shell templates/momentum.sh
#   ./las_shell templates/momentum.sh --backtest 2020-01-01:2023-12-31
#
# This strategy:
#   1. Asserts pre-trade conditions
#   2. Fires at market open via @time
#   3. Runs the full 5-stage pipeline with risk gate
#   4. Logs executions and P&L via |> operator
#   5. Flattens positions at 15:55
# ═══════════════════════════════════════════════════════════════

setmarket NYSE
setbroker IBKR
setaccount PAPER
setcapital 100000
setenv LOGDIR $LAS_SHELL_HOME/logs
setenv PIPELINE $LAS_SHELL_HOME/pipeline
setenv LOOKBACK 20
setenv TOP_N 10
setenv MAX_POS 1000

echo ══════════════════════════════════════════════════════════════
echo   Las_shell Momentum Strategy
echo ══════════════════════════════════════════════════════════════
echo Capital  : $CAPITAL
echo Account  : $ACCOUNT
echo Market   : $MARKET
echo Lookback : $LOOKBACK days
echo Top N    : $TOP_N positions
echo Started  : $(date +%Y-%m-%dT%H:%M:%S)
echo ""

# ── Pre-trade assertions ─────────────────────────────────────────
assert $CAPITAL > 10000 || exit 1
assert $ACCOUNT == PAPER || assert $ACCOUNT == LIVE || exit 1
echo "✔ Pre-trade assertions passed"
echo ""

# ── Ensure log directory exists ──────────────────────────────────
python3 -c "import os; os.makedirs(os.environ.get('LOGDIR','logs'), exist_ok=True)"

# ── Wait for market open, then run strategy ──────────────────────
echo "── Waiting for market open (@09:30:00) ─────────────────────"
@09:30:00 echo "Market open — launching momentum pipeline"

echo ""
echo "── Running 5-stage momentum pipeline ──────────────────────"

python3 $PIPELINE/universe.py \
  | python3 $PIPELINE/momentum_filter.py --threshold 0.2 --topn $TOP_N \
  | python3 $PIPELINE/risk_filter.py --max_size $MAX_POS \
  | python3 $PIPELINE/size_positions.py --model signal --capital $CAPITAL \
  ?> python3 ../scripts/risk_check.py \
  | python3 $PIPELINE/execute.py --mode paper |> $LOGDIR/trades.csv

echo ""
echo "── Today's executions ──────────────────────────────────────"
cat $LOGDIR/trades.csv

# ── Monitor P&L every 60 seconds ────────────────────────────────
echo ""
echo "── Monitoring P&L (watch 60, Ctrl+C to stop) ───────────────"
watch 60 python3 ../scripts/pnl_report.py |> $LOGDIR/daily_pnl.csv

# ── Flatten all positions before close ──────────────────────────
echo ""
echo "── Waiting for pre-close flatten (@15:55:00) ───────────────"
@15:55:00 flatten
python3 ../scripts/pnl_report.py

echo ""
echo "Strategy complete: $(date +%Y-%m-%dT%H:%M:%S)"
echo ══════════════════════════════════════════════════════════════
