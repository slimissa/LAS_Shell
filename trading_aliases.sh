#!/usr/bin/env las_shell
# ═══════════════════════════════════════════════════════════════
# QShell Trading Alias Library
# Loaded automatically by init_aliases() when MARKET env var is set.
# Chain-expands via the existing iterative expand_aliases() loop.
# ═══════════════════════════════════════════════════════════════

# ── Market Status ────────────────────────────────────────────────
alias mstatus='cat ~/.qshell_market'
alias mpnl='cat ~/.qshell_pnl'
alias mopen='echo "OPEN $(date +%H:%M:%S)" > ~/.qshell_market'
alias mclose='echo "CLOSED 0" > ~/.qshell_market'

# ── Position Management ──────────────────────────────────────────
alias positions='python3 $QSHELL_HOME/scripts/positions.py'
alias close_all='python3 $QSHELL_HOME/scripts/close_all.py'
alias flatten='positions && close_all && pnl'

# ── Risk ────────────────────────────────────────────────────────
alias check_risk='python3 $QSHELL_HOME/scripts/risk_check.py'
alias risk='assert $CAPITAL > 0 && check_risk'

# ── P&L ─────────────────────────────────────────────────────────
alias pnl='python3 $QSHELL_HOME/scripts/pnl_report.py'
alias pnl_log='pnl |> $QSHELL_HOME/logs/pnl.csv'
alias daily='pnl && echo "Daily report done"'

# ── Order Flow ───────────────────────────────────────────────────
alias send_orders='python3 $QSHELL_HOME/scripts/send_orders.py'
alias orders='send_orders ?> check_risk'
alias gen_orders='python3 $QSHELL_HOME/scripts/generate_orders.py'

# ── Strategy ────────────────────────────────────────────────────
alias backtest='python3 $QSHELL_HOME/scripts/backtest.py'
alias run_strat='gen_orders ?> check_risk && send_orders'
alias live='assert $ACCOUNT == LIVE && run_strat'
alias paper='assert $ACCOUNT == PAPER && run_strat'

# ── Audit & Logging ──────────────────────────────────────────────
alias audit='cat $QSHELL_HOME/logs/pnl.csv'
alias rejections='cat ~/.qshell_risk_rejections'
alias clear_log='echo "" > $QSHELL_HOME/logs/pnl.csv'

# ── Session ──────────────────────────────────────────────────────
alias morning='work && mstatus && positions && risk'
alias eod='flatten && daily && mclose && work off'
alias reset='unsetenv CAPITAL && setcapital 100000 && mclose'

# ── Watchlist ────────────────────────────────────────────────────
alias watchlist='cat $QSHELL_HOME/config/watchlist.txt'
alias add_watch='echo $1 >> $QSHELL_HOME/config/watchlist.txt'