#!/usr/bin/env las_shell
# Las_shell Strategy Template: Daily Momentum
# Phase 3.4 — templates/momentum_daily.sh

setmarket NYSE
setbroker IBKR
setaccount PAPER
setcapital 100000
setenv LOGDIR $LAS_SHELL_HOME/logs

echo "── Momentum Daily Strategy ──────────────────────────────────"
echo "Started: $(date +%Y-%m-%dT%H:%M:%S)"
echo "Capital: $CAPITAL  Account: $ACCOUNT"

assert $CAPITAL > 10000 || exit 1
assert ${DRAWDOWN:-0} < 5.0 || exit 1
echo "✔ Pre-trade checks passed"
echo ""

@09:30:00 universe SPY constituents \
    | momentum_filter --lookback 20 --top 10 \
    | risk_filter --max-pos 1000 \
    | size_positions --capital $CAPITAL \
    ?> risk_check.py \
    | execute |> $LOGDIR/trades.csv

watch 60 pnl |> $LOGDIR/daily_pnl.csv

@15:55:00 flatten
