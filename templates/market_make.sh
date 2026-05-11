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

@09:30:00 universe AAPL,MSFT,SPY \
    | market_maker --spread-bps 5 --size 100 \
    | risk_filter --max-pos 1000 \
    ?> risk_check.py \
    | execute |> $LOGDIR/trades.csv

watch 30 positions &
watch 300 pnl |> $LOGDIR/daily_pnl.csv

@15:45:00 flatten