#!/usr/bin/env las_shell
# Las_shell Strategy Template: Mean Reversion (Pairs Trading)
# Phase 3.4 — templates/mean_reversion.sh

setmarket NYSE
setbroker IBKR
setaccount PAPER
setcapital 100000
setenv LOGDIR $LAS_SHELL_HOME/logs
setenv PAIR_A SPY
setenv PAIR_B QQQ
setenv POS_SIZE 500

echo "── Mean Reversion Pairs Strategy ────────────────────────────"
echo "Started: $(date +%Y-%m-%dT%H:%M:%S)"
echo "Capital: $CAPITAL  Account: $ACCOUNT  Pair: $PAIR_A/$PAIR_B"

assert $CAPITAL > 10000 || exit 1
assert ${DRAWDOWN:-0} < 3.0 || exit 1

# FIX TR1: "universe SPY,QQQ pairs" used a "pairs" subcommand that
# doesn't exist in either universe.c or universe.py -- both only parse
# --watchlist/--symbols/--top, so the positional args (including
# "pairs") were silently ignored and the whole invocation ran against
# the default watchlist instead of the SPY/QQQ pair. Also fixes TM1
# (bare pairs_spread/risk_filter/execute names not on PATH). There is
# no JSON-pipeline "pairs" stage -- gen_pairs_orders.py (which wraps
# pairs_spread.py) is the actual, working pairs-trading entry point;
# use that instead of forcing this through the single-symbol pipeline.
@09:30:00 python3 $LAS_SHELL_HOME/scripts/gen_pairs_orders.py \
    && cat $LOGDIR/pairs_orders.txt \
    ?> python3 $LAS_SHELL_HOME/scripts/risk_check.py --max_size 2000 --max_notional 200000 \
    | python3 $LAS_SHELL_HOME/scripts/order_executor.py --mode paper |> $LOGDIR/trades.csv

{ watch 120 python3 $LAS_SHELL_HOME/scripts/pnl_report.py |> $LOGDIR/daily_pnl.csv } &

@15:50:00 flatten
