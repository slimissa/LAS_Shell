#!/usr/bin/env las_shell
# Las_shell Strategy Template: Daily Momentum
# Phase 3.4 — templates/momentum_daily.sh

setmarket NYSE
setbroker IBKR
setaccount PAPER
setcapital 100000
setenv LOGDIR $LAS_SHELL_HOME/logs
setenv PIPELINE $LAS_SHELL_HOME/pipeline

echo "── Momentum Daily Strategy ──────────────────────────────────"
echo "Started: $(date +%Y-%m-%dT%H:%M:%S)"
echo "Capital: $CAPITAL  Account: $ACCOUNT"

assert $CAPITAL > 10000 || exit 1
assert ${DRAWDOWN:-0} < 5.0 || exit 1
echo "✔ Pre-trade checks passed"
echo ""

# FIX TM1/TD6: previous version used bare command names (universe,
# momentum_filter, risk_filter, size_positions, risk_check.py, execute)
# that aren't builtins and aren't on PATH by default -- every stage
# failed with "command not found". Also "universe SPY constituents"
# passed a positional arg universe.py/universe.c never reads (both only
# accept --watchlist/--symbols/--top; see TR1 fix in mean_reversion.sh
# for the same class of bug). Use full paths and the flags the stages
# actually implement, matching the verified-working pattern in
# templates/momentum.sh.
@09:30:00 python3 $PIPELINE/universe.py --top 10 \
    | python3 $PIPELINE/momentum_filter.py --threshold 0.2 --topn 10 \
    | python3 $PIPELINE/risk_filter.py --max_size 1000 \
    | python3 $PIPELINE/size_positions.py --model signal --capital $CAPITAL \
    ?> python3 $LAS_SHELL_HOME/scripts/risk_check.py \
    | python3 $PIPELINE/execute.py --mode paper |> $LOGDIR/trades.csv

{ watch 60 python3 $LAS_SHELL_HOME/scripts/pnl_report.py |> $LOGDIR/daily_pnl.csv } &

@15:55:00 flatten
