#!/usr/bin/env las_shell
# ═══════════════════════════════════════════════════════════════
# templates/backtest_runner.sh — Multi-Period Backtest Runner
# ═══════════════════════════════════════════════════════════════

setenv STRATEGY templates/momentum_daily.sh
setenv LOGDIR $QSHELL_HOME/logs
setenv BT_CAPITAL 100000
setenv MIN_SHARPE 0.5
setenv MAX_DRAWDOWN_PCT 20
setenv PASS_THRESHOLD 3

setmarket NYSE
setcapital $BT_CAPITAL
setaccount PAPER

echo "══════════════════════════════════════════════════════════════"
echo "  QShell Backtest Runner"
echo "══════════════════════════════════════════════════════════════"
echo "Strategy  : $STRATEGY"
echo "Capital   : $BT_CAPITAL"
echo "Min Sharpe: $MIN_SHARPE"
echo "Max DD    : ${MAX_DRAWDOWN_PCT}%"
echo "Started   : $(date +%Y-%m-%dT%H:%M:%S)"
echo ""

assert $BT_CAPITAL > 0 || exit 1
echo "✔ Configuration valid"
echo ""

echo "── Running 8-period backtest ────────────────────────────────"
python3 -c "import os; os.makedirs(os.environ.get('LOGDIR', 'logs'), exist_ok=True)"
python3 $QSHELL_HOME/scripts/run_backtest.py

echo ""
echo "── Results logged ───────────────────────────────────────────"
cat $LOGDIR/backtest_summary.csv |> $LOGDIR/bt_run_history.csv
cat $LOGDIR/backtest_summary.csv

echo ""
echo "── Detail reports ───────────────────────────────────────────"
python3 -c "import os; files=os.listdir('$LOGDIR/backtest_detail'); print('\n'.join(sorted(files)))"

echo ""
echo "Backtest complete: $(date +%Y-%m-%dT%H:%M:%S)"
echo "══════════════════════════════════════════════════════════════"