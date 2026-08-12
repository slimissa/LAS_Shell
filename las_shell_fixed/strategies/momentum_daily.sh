#!/usr/bin/env las_shell
# ═══════════════════════════════════════════════════════════════
# templates/momentum_daily.sh — Daily Momentum Strategy
# ═══════════════════════════════════════════════════════════════

setmarket NYSE
setcapital 100000
setaccount PAPER
setenv LOGDIR $LAS_SHELL_HOME/logs

echo "── Momentum Daily Strategy ──────────────────────────────────"
echo "Started: $(date +%Y-%m-%dT%H:%M:%S)"
echo "Capital: $CAPITAL  Account: $ACCOUNT"

assert $CAPITAL > 10000 || exit 1
echo "✔ Pre-trade checks passed"

echo ""
echo "── Scanning momentum signals ────────────────────────────────"
python3 $LAS_SHELL_HOME/scripts/momentum.py AAPL MSFT GOOGL AMZN TSLA META NVDA SPY QQQ --lookback 20 --topn 5 |> $LOGDIR/momentum_signals.csv

cat $LOGDIR/momentum_signals.csv

echo ""
echo "── Generating orders ────────────────────────────────────────"
python3 $LAS_SHELL_HOME/scripts/gen_momentum_orders.py

# FIX TD1: the generated orders in $LOGDIR/momentum_orders.txt were never
# actually risk-gated -- the line below checked a hardcoded literal string
# ("AAPL BUY 100 185.50") that had nothing to do with what was just
# generated, so real orders were never validated (or executed at all).
# Route the ACTUAL generated orders through the gate and into execution.
echo ""
echo "── Routing orders through risk gate ────────────────────────"
if [ -s "$LOGDIR/momentum_orders.txt" ]; then
    cat $LOGDIR/momentum_orders.txt ?> python3 $LAS_SHELL_HOME/scripts/risk_check.py | python3 $LAS_SHELL_HOME/scripts/order_executor.py --mode paper |> $LOGDIR/trades.csv
else
    echo "No orders generated this cycle"
fi

echo ""
echo "── P&L Report ───────────────────────────────────────────────"
python3 $LAS_SHELL_HOME/scripts/pnl_report.py

echo ""
echo "Strategy complete: $(date +%Y-%m-%dT%H:%M:%S)"
