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
python3 ../scripts/pairs_spread.py $PAIR_A $PAIR_B --lookback 60 --entry $ENTRY_Z --exit $EXIT_Z |> $LOGDIR/pairs_signals.csv
python3 ../scripts/pairs_spread.py $PAIR_A $PAIR_B --lookback 60 --entry $ENTRY_Z --exit $EXIT_Z

echo ""
echo "── Building orders ──────────────────────────────────────────"
python3 ../scripts/gen_pairs_orders.py

echo ""
echo "── Risk gating orders (?>) ──────────────────────────────────"
python3 ../scripts/gen_pairs_orders.py
echo "AAPL BUY 500 185.50" ?> python3 ../scripts/risk_check.py --max_notional 200000 --max_size 2000 && echo "Risk gate: PASS" || echo "Risk gate: REJECTED"

echo ""
echo "── Verifying hedge ratio ────────────────────────────────────"
python3 ../scripts/check_hedge_ratio.py

echo ""
python3 ../scripts/pnl_report.py
echo ""
echo "Pairs strategy complete: $(date +%Y-%m-%dT%H:%M:%S)"