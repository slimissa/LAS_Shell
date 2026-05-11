#!/usr/bin/env las_shell
# Las_shell Strategy Template: Backtest Runner
# Phase 3.4 — templates/backtest_runner.sh

setmarket NYSE
setbroker IBKR
setaccount PAPER
setcapital 100000
setenv LOGDIR $HOME/las_shell_logs
setenv PIPELINE $LAS_SHELL_HOME/pipeline

python3 -c "import os; os.makedirs(os.environ.get('LOGDIR', '$HOME/las_shell_logs'), exist_ok=True)"

echo "══════════════════════════════════════════════════════════════"
echo "  Las_shell Backtest Runner — 4-Year Momentum Backtest"
echo "══════════════════════════════════════════════════════════════"
echo "Capital: $CAPITAL  Account: $ACCOUNT"
echo "Started: $(date +%Y-%m-%dT%H:%M:%S)"
echo ""

assert $CAPITAL > 0 || exit 1
echo "✔ Configuration valid"
echo ""

echo "── Running 2020 backtest ────────────────────────────────────"
python3 $PIPELINE/universe.py \
    | python3 $PIPELINE/momentum_filter.py --lookback 20 --top 10 \
    | python3 $PIPELINE/risk_filter.py \
    | python3 $PIPELINE/size_positions.py --capital $CAPITAL \
    | python3 $PIPELINE/execute.py ~> 2020-01-01:2020-12-31 \
    |> $LOGDIR/backtest_2020.csv

echo "── Running 2021 backtest ────────────────────────────────────"
python3 $PIPELINE/universe.py \
    | python3 $PIPELINE/momentum_filter.py --lookback 20 --top 10 \
    | python3 $PIPELINE/risk_filter.py \
    | python3 $PIPELINE/size_positions.py --capital $CAPITAL \
    | python3 $PIPELINE/execute.py ~> 2021-01-01:2021-12-31 \
    |> $LOGDIR/backtest_2021.csv

echo "── Running 2022 backtest ────────────────────────────────────"
python3 $PIPELINE/universe.py \
    | python3 $PIPELINE/momentum_filter.py --lookback 20 --top 10 \
    | python3 $PIPELINE/risk_filter.py \
    | python3 $PIPELINE/size_positions.py --capital $CAPITAL \
    | python3 $PIPELINE/execute.py ~> 2022-01-01:2022-12-31 \
    |> $LOGDIR/backtest_2022.csv

echo "── Running 2023 backtest ────────────────────────────────────"
python3 $PIPELINE/universe.py \
    | python3 $PIPELINE/momentum_filter.py --lookback 20 --top 10 \
    | python3 $PIPELINE/risk_filter.py \
    | python3 $PIPELINE/size_positions.py --capital $CAPITAL \
    | python3 $PIPELINE/execute.py ~> 2023-01-01:2023-12-31 \
    |> $LOGDIR/backtest_2023.csv

echo ""
echo "── Backtest Results ─────────────────────────────────────────"
cat $LOGDIR/backtest_2020.csv $LOGDIR/backtest_2021.csv \
    $LOGDIR/backtest_2022.csv $LOGDIR/backtest_2023.csv \
    |> $LOGDIR/backtest_all_years.csv

echo ""
pnl
echo ""
echo "Backtest complete: $(date +%Y-%m-%dT%H:%M:%S)"
echo "══════════════════════════════════════════════════════════════"