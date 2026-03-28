#!/usr/bin/env las_shell
# ═══════════════════════════════════════════════════════════════
# templates/momentum_daily.sh — Daily Momentum Strategy
# ═══════════════════════════════════════════════════════════════

setmarket NYSE
setcapital 100000
setaccount PAPER
setenv LOGDIR $QSHELL_HOME/logs

echo "── Momentum Daily Strategy ──────────────────────────────────"
echo "Started: $(date +%Y-%m-%dT%H:%M:%S)"
echo "Capital: $CAPITAL  Account: $ACCOUNT"

assert $CAPITAL > 10000 || exit 1
echo "✔ Pre-trade checks passed"

echo ""
echo "── Scanning momentum signals ────────────────────────────────"
python3 $QSHELL_HOME/scripts/momentum.py AAPL MSFT GOOGL AMZN TSLA META NVDA SPY QQQ --lookback 20 --topn 5 |> $LOGDIR/momentum_signals.csv

cat $LOGDIR/momentum_signals.csv

echo ""
echo "── Generating orders ────────────────────────────────────────"
python3 $QSHELL_HOME/scripts/gen_momentum_orders.py

echo ""
echo "── Routing orders through risk gate ────────────────────────"
echo "AAPL BUY 100 185.50" ?> python3 $QSHELL_HOME/scripts/risk_check.py && echo "Risk gate: PASS" || echo "Risk gate: REJECTED"
python3 $QSHELL_HOME/scripts/gen_momentum_orders.py

echo ""
echo "── P&L Report ───────────────────────────────────────────────"
python3 $QSHELL_HOME/scripts/pnl_report.py

echo ""
echo "Strategy complete: $(date +%Y-%m-%dT%H:%M:%S)"