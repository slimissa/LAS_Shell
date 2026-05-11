#!/usr/bin/env las_shell
# ═══════════════════════════════════════════════════════════════
# templates/market_make.sh — Market Making Strategy
# ═══════════════════════════════════════════════════════════════

setmarket NYSE
setcapital 200000
setaccount PAPER
setenv TICKER AAPL
setenv CYCLES 3
setenv SPREAD_BPS 5
setenv QUOTE_SIZE 100
setenv MAX_INVENTORY 500
setenv MAX_DRAWDOWN 2000
setenv LOGDIR $LAS_SHELL_HOME/logs

echo "── Market Making Strategy ───────────────────────────────────"
echo "Started   : $(date +%Y-%m-%dT%H:%M:%S)"
echo "Ticker    : $TICKER"
echo "Spread    : ${SPREAD_BPS}bps  Size: $QUOTE_SIZE  MaxInv: $MAX_INVENTORY"
echo "Account   : $ACCOUNT  Capital: $CAPITAL"
echo ""

assert $CAPITAL >= 50000 || { echo "ABORT: need >= $50,000 capital"; exit 1; }
assert $SPREAD_BPS >= 3  || { echo "ABORT: spread too tight (< 3bps)"; exit 1; }
assert $CYCLES >= 1      || { echo "ABORT: need at least 1 cycle"; exit 1; }
echo "✔ Pre-flight checks passed"
echo ""


echo "── Running $CYCLES quoting cycles ──────────────────────────"
setenv CYCLE 1
while [ $CYCLE -le $CYCLES ]; do
    assert $DRAWDOWN < $MAX_DRAWDOWN || { echo "Cycle $CYCLE: ABORT — drawdown limit breached"; exit 1; }
    python3 ../scripts/market_maker.py $TICKER --size $QUOTE_SIZE --spread_bps $SPREAD_BPS --max_pos $MAX_INVENTORY \
        ?> python3 ../scripts/risk_check.py --max_notional 100000 --max_size $MAX_INVENTORY \
        && echo "Cycle $CYCLE: quote accepted" |> $LOGDIR/mm_executions.csv \
        || echo "Cycle $CYCLE: quote rejected"
    setenv CYCLE $(expr $CYCLE + 1)
done

echo ""
echo "── Execution audit ──────────────────────────────────────────"
cat $LOGDIR/mm_executions.csv

echo ""
python3 ../scripts/pnl_report.py

echo ""
echo "Market making complete: $(date +%Y-%m-%dT%H:%M:%S)"