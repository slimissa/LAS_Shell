#!/usr/bin/env las_shell
# ═══════════════════════════════════════════════════════════════
# templates/mean_reversion.sh — Pairs Trading / Mean Reversion
# ═══════════════════════════════════════════════════════════════

setmarket NYSE
setcapital 100000
setaccount PAPER
setenv PAIR_A AAPL
setenv PAIR_B MSFT
setenv ENTRY_Z 2.0
setenv EXIT_Z  0.5
setenv POS_SIZE 500
setenv LOGDIR $LAS_SHELL_HOME/logs

echo "── Mean Reversion Pairs Strategy ────────────────────────────"
echo "Started : $(date +%Y-%m-%dT%H:%M:%S)"
echo "Pair    : $PAIR_A / $PAIR_B"
echo "Capital : $CAPITAL   Account: $ACCOUNT"
echo ""

assert $CAPITAL >= 25000 || { echo "ABORT: insufficient capital"; exit 1; }
echo "✔ Risk assertions passed"

echo ""
echo "── Computing spread: $PAIR_A / $PAIR_B ─────────────────────"
python3 -c "import os; os.makedirs(os.environ.get('LOGDIR', 'logs'), exist_ok=True)"
python3 $LAS_SHELL_HOME/scripts/pairs_spread.py $PAIR_A $PAIR_B --lookback 60 --entry $ENTRY_Z --exit $EXIT_Z |> $LOGDIR/pairs_signals.csv
cat $LOGDIR/pairs_signals.csv

echo ""
echo "── Building orders ──────────────────────────────────────────"
python3 $LAS_SHELL_HOME/scripts/gen_pairs_orders.py

# FIX MR3: this used to call gen_pairs_orders.py a second time for no
# reason, then risk-gate a hardcoded literal string ("AAPL BUY 500 185.50")
# that had nothing to do with the orders actually just generated -- the
# real orders in $LOGDIR/pairs_orders.txt were never validated or executed
# at all. Same bug as TD1 in momentum_daily.sh, fixed the same way: route
# the actual generated orders through the gate and into execution.
echo ""
echo "── Routing orders through risk gate ─────────────────────────"
if [ -s "$LOGDIR/pairs_orders.txt" ]; then
    cat $LOGDIR/pairs_orders.txt ?> python3 $LAS_SHELL_HOME/scripts/risk_check.py --max_notional 200000 --max_size 2000 | python3 $LAS_SHELL_HOME/scripts/order_executor.py --mode paper |> $LOGDIR/pairs_trades.csv
else
    echo "No orders generated this cycle"
fi

echo ""
echo "── Verifying hedge ratio ────────────────────────────────────"
python3 $LAS_SHELL_HOME/scripts/check_hedge_ratio.py --log $LOGDIR/pairs_trades.csv

echo ""
python3 $LAS_SHELL_HOME/scripts/pnl_report.py
echo ""
echo "Pairs strategy complete: $(date +%Y-%m-%dT%H:%M:%S)"