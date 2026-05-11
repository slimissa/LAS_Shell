#ifndef RISK_CONFIG_H
#define RISK_CONFIG_H

/* ============================================================
 * Las_shell  —  Phase 4.3  :  Risk Limit Configuration
 * risk_config.h  —  Public interface
 *
 * ~/.las_shell_risk  is parsed once at startup into a singleton
 * RiskConfig.  Every subsequent call to validate_order_against_risk()
 * and assert_risk_limits() reads from that global struct —  zero
 * file I/O on the hot path.
 *
 * Config file format (one directive per line, '#' = comment):
 *
 *   MAX_POSITION_SIZE   = 1000
 *   MAX_DRAWDOWN_PCT    = 5.0
 *   MAX_DAILY_LOSS      = 2000
 *   MAX_ORDER_NOTIONAL  = 500000
 *   ALLOWED_SYMBOLS     = SPY,QQQ,IWM,AAPL,MSFT
 *   BLOCKED_SYMBOLS     = GME,AMC,BBBY
 *   MAX_ORDERS_PER_DAY  = 50
 *   MIN_POSITION_SIZE   = 1
 *
 * Unrecognised keys are silently ignored so new keys can be added
 * without breaking old config files.
 * ============================================================ */

#include <stddef.h>   /* size_t  */

/* ── Limits ────────────────────────────────────────────────── */
#define RISK_MAX_SYMBOLS 256   /* max tickers in ALLOWED / BLOCKED list */
#define RISK_SYMBOL_LEN   16   /* max length of a single ticker symbol   */
#define RISK_CONFIG_PATH  "/.las_shell_risk"   /* relative to $HOME         */

/* ── RiskConfig — the singleton loaded from ~/.las_shell_risk ── */
typedef struct {
    /* --- position / order limits -------------------------------- */
    double  max_position_size;    /* MAX_POSITION_SIZE  (shares/units)   */
    double  min_position_size;    /* MIN_POSITION_SIZE  (shares/units)   */
    double  max_order_notional;   /* MAX_ORDER_NOTIONAL ($)              */
    int     max_orders_per_day;   /* MAX_ORDERS_PER_DAY (count)          */

    /* --- P&L / drawdown limits ---------------------------------- */
    double  max_drawdown_pct;     /* MAX_DRAWDOWN_PCT   (%)              */
    double  max_daily_loss;       /* MAX_DAILY_LOSS     ($)              */

    /* --- symbol allow/block lists ------------------------------- */
    int     has_allowed_list;     /* 0 = all symbols allowed             */
    int     allowed_count;
    char    allowed_symbols[RISK_MAX_SYMBOLS][RISK_SYMBOL_LEN];

    int     blocked_count;
    char    blocked_symbols[RISK_MAX_SYMBOLS][RISK_SYMBOL_LEN];

    /* --- meta --------------------------------------------------- */
    int     loaded;               /* 1 if config was successfully read   */
    char    config_path[512];     /* resolved path that was loaded       */
} RiskConfig;


/* ── Validation result ────────────────────────────────────── */
#define RISK_PASS             0
#define RISK_ERR_SYMBOL       1    /* symbol not in allowed list / blocked */
#define RISK_ERR_SIZE         2    /* position size out of range           */
#define RISK_ERR_NOTIONAL     3    /* notional value too large             */
#define RISK_ERR_DRAWDOWN     4    /* drawdown limit breached              */
#define RISK_ERR_DAILY_LOSS   5    /* daily loss limit breached            */
#define RISK_ERR_ORDER_COUNT  6    /* too many orders today                */
#define RISK_ERR_CONFIG       7    /* config error / internal fault        */

typedef struct {
    int    code;                   /* RISK_PASS or RISK_ERR_*             */
    char   reason[256];            /* human-readable rejection reason     */
    char   field[64];              /* which limit was breached            */
    double limit_value;            /* the configured limit                */
    double actual_value;           /* the value that was checked          */
} RiskResult;


/* ── Parsed order —  what validate_order_against_risk() reads ─
 *
 * Input order lines follow the existing Las_shell convention:
 *   SYMBOL  ACTION  SIZE  [PRICE]
 *   e.g.  "SPY BUY 100 485.20"   or   "AAPL SELL 50"
 * ─────────────────────────────────────────────────────────── */
typedef struct {
    char    symbol[RISK_SYMBOL_LEN];
    char    action[16];         /* BUY | SELL | SHORT | COVER   */
    double  size;              /* number of shares / contracts  */
    double  price;             /* 0.0 = unknown / market order  */
    double  notional;          /* size * price (0 if no price)  */
} ParsedOrder;


/* ============================================================
 * Public API
 * ============================================================ */

/*
 * load_risk_config()
 *   Read ~/.las_shell_risk into the global singleton.
 *   Safe to call multiple times — reloads the file on each call.
 *   Called from main() during shell startup.
 */
void load_risk_config(void);

/*
 * reload_risk_config()
 *   Force a re-read from disk (for use by "riskconfig reload").
 */
void reload_risk_config(void);

/*
 * get_risk_config()
 *   Return a const pointer to the global singleton.
 *   Returns NULL if load_risk_config() has never been called.
 */
const RiskConfig* get_risk_config(void);

/*
 * validate_order_against_risk()
 *   Parse an order line and check every applicable limit.
 *   Populates *result and returns RISK_PASS (0) or an error code.
 *
 *   Called by execute_risk_gate() in redirection.c before forwarding
 *   output to the right side of  ?>
 *
 *   If no config has been loaded (file absent), all orders pass.
 */
int validate_order_against_risk(const char* order_line, RiskResult* result);

/*
 * assert_risk_limits()
 *   Called by command_assert() when invoked with *no* explicit operands:
 *       assert
 *   Checks the current runtime state (DRAWDOWN, DAILY_LOSS, etc.) read
 *   from environment variables against the loaded limits.
 *   Writes first failure to *result; returns RISK_PASS or error code.
 */
int assert_risk_limits(char** env, RiskResult* result);

/*
 * risk_result_print()
 *   Print a human-readable one-line summary of a RiskResult to stderr.
 */
void risk_result_print(const RiskResult* r);

/*
 * command_riskconfig()
 *   Built-in:  riskconfig [show | reload | path]
 *   show   — pretty-print loaded limits
 *   reload — re-read ~/.las_shell_risk
 *   path   — print resolved config file path
 */
int command_riskconfig(char** args, char** env);

/*
 * print_risk_config()
 *   Pretty-print the currently loaded RiskConfig (used by 'riskconfig show').
 */
void print_risk_config(void);

#endif /* RISK_CONFIG_H */
