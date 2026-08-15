/*
 * broker.h — Las_shell Phase 4.2: Broker API Bridge — Public Header
 *
 * Declares every symbol that other translation units (main.c, Commands.c,
 * audit.c) need from broker.c.  broker.c is the only file that should
 * include this header directly; everyone else goes through my_own_shell.h.
 *
 * Architecture recap
 * ──────────────────
 *  ┌──────────────────────────────────────────────────────┐
 *  │  Las_shell built-in: order / positions / balance / …   │
 *  │  (dispatched from main.c execute_command_line_env)   │
 *  └──────────────┬───────────────────────────────────────┘
 *                 │ ACCOUNT env var
 *         ┌───── ▼ ──────┐
 *         │ PAPER        │  LIVE (any value ≠ PAPER)
 *         │              │
 *    ┌────▼────┐    ┌────▼───────────────────────────┐
 *    │ In-proc │    │ libcurl HTTP to $BROKER_API     │
 *    │ ledger  │    │  ┌─── Alpaca adapter           │
 *    │ (fast   │    │  ├─── IBKR adapter             │
 *    │  path)  │    │  └─── Generic REST adapter     │
 *    └────┬────┘    └────────────────┬────────────────┘
 *         │                          │
 *    ┌────▼──────────────────────────▼────┐
 *    │  ~/.las_shell_order_log  (CSV append) │  ← every fill
 *    │  ~/.las_shell_paper_account (state)   │  ← paper only
 *    └────────────────────────────────────┘
 *
 * Broker adapters
 * ───────────────
 *  BROKER=ALPACA  → uses Alpaca v2 REST API (api.alpaca.markets)
 *  BROKER=IBKR    → uses IB Client Portal Gateway REST API
 *  BROKER=<other> → generic adapter: POST $BROKER_API/orders etc.
 *
 * Paper simulation server (optional)
 * ───────────────────────────────────
 *  When ACCOUNT=PAPER and BROKER_API is set, requests are routed to that
 *  endpoint (so sim_server.py can be used as a drop-in paper broker).
 *  When BROKER_API is NOT set, the in-process ledger is used (default).
 */

#ifndef LAS_SHELL_BROKER_H
#define LAS_SHELL_BROKER_H

/* ── Public command entry points ────────────────────────────────────────
 * All follow the standard Las_shell built-in signature.
 * Return 0 on success, non-zero on error.
 */

/*
 * order buy|sell TICKER SIZE [market|limit PRICE] [--tif DAY|GTC|IOC]
 *
 *   order buy  SPY 100 market
 *   order sell AAPL 50 limit 185.00
 *   order buy  QQQ 200 market --tif IOC
 */
int command_order(char** args, char** env);

/*
 * positions [--json] [--symbol TICKER]
 *
 * Print the current position table.  --json emits raw JSON suitable for
 * piping to jq.  --symbol filters to a single ticker.
 */
int command_positions(char** args, char** env);

/*
 * balance [--json]
 *
 * Print account equity summary.  --json emits structured JSON.
 */
int command_balance(char** args, char** env);

/*
 * cancel ORDER_ID
 *
 * Cancel a pending (open) order by its order ID.  In paper mode all
 * orders fill immediately so cancel always succeeds with a notice.
 */
int command_cancel(char** args, char** env);

/*
 * close_all
 *
 * Flatten every open position.  Used by the 'flatten' alias.
 */
int command_close_all(char** args, char** env);

/*
 * reset_paper [--capital AMOUNT]
 *
 * Reset the paper account ledger to initial state.
 * --capital overrides the default $100,000 starting cash.
 */
int command_reset_paper(char** args, char** env);

/*
 * broker_status
 *
 * Print the current broker configuration and connectivity status.
 */
int command_broker_status(char** args, char** env);

/* ── Called from pnl_update path ────────────────────────────────────────
 * Returns the current total P&L (equity − starting_cash) for the
 * prompt daemon.  Writes the value to ~/.las_shell_pnl.
 */
double broker_get_pnl(char** env);

/* ── Paper account state reset (used by test harness) ─────────────────── */
void broker_paper_reset_state(void);

#endif /* LAS_SHELL_BROKER_H */
