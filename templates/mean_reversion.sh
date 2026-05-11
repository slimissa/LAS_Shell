#!/usr/bin/env las_shell
# Las_shell Strategy Template: Mean Reversion (Pairs Trading)
# Phase 3.4 — templates/mean_reversion.sh

setmarket NYSE
setbroker IBKR
setaccount PAPER
setcapital 100000
setenv LOGDIR $LAS_SHELL_HOME/logs

echo "── Mean Reversion Pairs Strategy ────────────────────────────"
echo "Started: $(date +%Y-%m-%dT%H:%M:%S)"
echo "Capital: $CAPITAL  Account: $ACCOUNT"

assert $CAPITAL > 10000 || exit 1
assert ${DRAWDOWN:-0} < 3.0 || exit 1

@09:30:00 universe SPY,QQQ pairs \
    | pairs_spread --zscore-threshold 2.0 \
    | risk_filter --max-pos 500 --max-drawdown 3% \
    | size_positions --capital $CAPITAL \
    ?> risk_check.py \
    | execute |> $LOGDIR/trades.csv

watch 120 pnl |> $LOGDIR/daily_pnl.csv

@15:50:00 flatten