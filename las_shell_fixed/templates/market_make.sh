#!/usr/bin/env las_shell
# Las_shell Strategy Template: Market Making (Bid/Ask Spread Capture)
# Phase 3.4 — templates/market_make.sh

setmarket NYSE
setbroker IBKR
setaccount PAPER
setcapital 250000
setenv SPREAD_BPS 5
setenv MAX_INVENTORY 1000
setenv LOGDIR $LAS_SHELL_HOME/logs

echo "── Market Making Strategy ───────────────────────────────────"
echo "Started: $(date +%Y-%m-%dT%H:%M:%S)"
echo "Capital: $CAPITAL  Account: $ACCOUNT  Spread: ${SPREAD_BPS}bps"

assert $CAPITAL > 50000 || exit 1
assert $SPREAD_BPS >= 3 || exit 1

# FIX TM1: bare "market_maker"/"risk_filter"/"risk_check.py"/"execute"
# aren't on PATH by default. Also, risk_filter is a JSON-pipeline stage
# (see docs/PIPELINE_CONVENTION.md) but market_maker.py emits a plain
# "TICKER ACTION SIZE PRICE" order line (see the MM2 fix), not JSON --
# the two were never compatible, so risk_filter is dropped here in
# favor of risk_check.py, which already speaks the plain order format
# market_maker.py actually produces.
@09:30:00 python3 $LAS_SHELL_HOME/scripts/market_maker.py AAPL --spread_bps $SPREAD_BPS --size 100 --max_pos $MAX_INVENTORY \
    ?> python3 $LAS_SHELL_HOME/scripts/risk_check.py --max_size $MAX_INVENTORY \
    | python3 $LAS_SHELL_HOME/scripts/order_executor.py --mode paper |> $LOGDIR/trades.csv

watch 30 positions &
{ watch 300 python3 $LAS_SHELL_HOME/scripts/pnl_report.py |> $LOGDIR/daily_pnl.csv } &

@15:45:00 flatten
