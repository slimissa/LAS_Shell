#!/usr/bin/env las_shell
# ═══════════════════════════════════════════════════════════════
# Las_shell Trading Alias Library  —  REFERENCE ONLY, NOT LOADED
#
# FIX TA1/TA2/TA3: this file is NOT loaded automatically (or at all) by
# the shell. The comment that used to be here claimed init_aliases()
# loads it when MARKET is set -- that was never true; verified against
# src/trading_aliases_init.c, which defines its own separate, hardcoded
# TA[] array of aliases and has no reference to this file anywhere.
# Every alias las_shell actually starts with (mstatus, mpnl, live,
# paper, flatten, pnl, ...) comes from that C array, not from here.
#
# This file also references scripts that don't exist in this project
# (positions.py, close_all.py, send_orders.py, generate_orders.py,
# backtest.py) and uses paths (../scripts/...) that only resolve if
# 'source'd from inside strategies/. Treat it as a legacy/reference
# sketch of what a fuller alias set might look like, not something to
# 'source' as-is. If you want these aliases live, they'd need to be
# added to trading_aliases_init.c's TA[] array (or this file fixed up
# and explicitly sourced from your shell startup), not just left here.
# ═══════════════════════════════════════════════════════════════

# ── Market Status ────────────────────────────────────────────────
alias mstatus='cat ~/.las_shell_market'
alias mpnl='cat ~/.las_shell_pnl'
alias mopen='echo "OPEN $(date +%H:%M:%S)" > ~/.las_shell_market'
alias mclose='echo "CLOSED 0" > ~/.las_shell_market'

# ── Position Management ──────────────────────────────────────────
alias positions='python3 ../scripts/positions.py'
alias close_all='python3 ../scripts/close_all.py'
alias flatten='positions && close_all && pnl'

# ── Risk ────────────────────────────────────────────────────────
alias check_risk='python3 ../scripts/risk_check.py'
alias risk='assert $CAPITAL > 0 && check_risk'

# ── P&L ─────────────────────────────────────────────────────────
alias pnl='python3 ../scripts/pnl_report.py'
alias pnl_log='pnl |> $LAS_SHELL_HOME/logs/pnl.csv'
alias daily='pnl && echo "Daily report done"'

# ── Order Flow ───────────────────────────────────────────────────
alias send_orders='python3 ../scripts/send_orders.py'
alias orders='gen_orders ?> check_risk && send_orders'
alias gen_orders='python3 ../scripts/generate_orders.py'

# ── Strategy ────────────────────────────────────────────────────
alias backtest='python3 ../scripts/backtest.py'
alias run_strat='gen_orders ?> check_risk && send_orders'
alias live='assert $ACCOUNT == LIVE && run_strat'
alias paper='assert $ACCOUNT == PAPER && run_strat'

# ── Audit & Logging ──────────────────────────────────────────────
alias audit='cat $LAS_SHELL_HOME/logs/pnl.csv'
alias rejections='cat ~/.las_shell_risk_rejections'
alias clear_log='echo "" > $LAS_SHELL_HOME/logs/pnl.csv'

# ── Session ──────────────────────────────────────────────────────
alias morning='work && mstatus && positions && risk'
alias eod='flatten && daily && mclose && work off'
alias reset='unsetenv CAPITAL && setcapital 100000 && mclose'

# ── Watchlist ────────────────────────────────────────────────────
alias watchlist='cat $LAS_SHELL_HOME/config/watchlist.txt'
alias add_watch='echo $1 >> $LAS_SHELL_HOME/config/watchlist.txt'