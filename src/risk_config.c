/* ============================================================
 * Las_shell  —  Phase 4.3  :  Risk Limit Configuration
 * risk_config.c
 *
 * Implements:
 *   · load_risk_config()           — parse ~/.las_shell_risk at startup
 *   · reload_risk_config()         — hot-reload from disk
 *   · get_risk_config()            — read-only access to singleton
 *   · validate_order_against_risk()— per-order limit check (called by ?>)
 *   · assert_risk_limits()         — zero-arg assert checks live env state
 *   · risk_result_print()          — human-readable rejection message
 *   · command_riskconfig()         — "riskconfig show|reload|path" built-in
 *   · print_risk_config()          — pretty-print loaded limits
 *
 * Design decisions
 *   - One global singleton loaded once at startup; zero per-order I/O.
 *   - Parser is line-oriented, key = value, '#' comments, tolerant of
 *     extra whitespace.  Unknown keys are silently skipped so the file
 *     is forward-compatible.
 *   - Symbol lists are stored as fixed-size 2-D char arrays (no heap)
 *     to make the struct trivially copyable and valgrind-clean.
 *   - validate_order_against_risk() is designed to be called from
 *     execute_risk_gate() in redirection.c; it takes a raw order line
 *     (the captured stdout of the left side of ?>) and returns a
 *     structured RiskResult so the caller can log exactly which limit
 *     fired and at what value.
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L

#include "../include/my_own_shell.h"
#include "../include/risk_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ── Singleton ──────────────────────────────────────────────── */
static RiskConfig g_risk_cfg;
static int        g_risk_loaded = 0;

/* ============================================================
 * Internal helpers
 * ============================================================ */

/* trim_ws — strip leading/trailing whitespace in-place, return start ptr */
static char* trim_ws(char* s) {
    if (!s) return s;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char* end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return s;
}

/* str_to_upper — upper-case a string in-place */
static void str_to_upper(char* s) {
    if (!s) return;
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

/* safe_strncpy — always NUL-terminates */
static void safe_strncpy(char* dst, const char* src, size_t n) {
    if (!dst || n == 0) return;
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

/* parse_symbol_list — "SPY,QQQ,IWM" → fills dst[][RISK_SYMBOL_LEN]
 * Returns number of symbols parsed. */
static int parse_symbol_list(const char* val,
                              char dst[][RISK_SYMBOL_LEN],
                              int  max_count) {
    int  count = 0;
    char buf[4096];
    safe_strncpy(buf, val, sizeof(buf));

    char* saveptr = NULL;
    char* tok = strtok_r(buf, ",", &saveptr);
    while (tok && count < max_count) {
        tok = trim_ws(tok);
        if (*tok != '\0') {
            str_to_upper(tok);
            safe_strncpy(dst[count], tok, RISK_SYMBOL_LEN);
            count++;
        }
        tok = strtok_r(NULL, ",", &saveptr);
    }
    return count;
}

/* ── Default limits (applied when config file is absent) ────
 * Conservative defaults that won't surprise a quant on day one.
 * All can be overridden in ~/.las_shell_risk.                      */
static void set_defaults(RiskConfig* cfg) {
    memset(cfg, 0, sizeof(RiskConfig));
    cfg->max_position_size   = 10000.0;   /* shares  */
    cfg->min_position_size   = 1.0;
    cfg->max_order_notional  = 1000000.0; /* $1M per order */
    cfg->max_orders_per_day  = 500;
    cfg->max_drawdown_pct    = 10.0;      /* 10 % drawdown  */
    cfg->max_daily_loss      = 50000.0;   /* $50k daily loss */
    cfg->has_allowed_list    = 0;         /* all symbols OK by default */
    cfg->allowed_count       = 0;
    cfg->blocked_count       = 0;
}


/* ============================================================
 * load_risk_config  /  reload_risk_config
 * ============================================================ */

/* parse_config_file — core parser.  Returns 0 on success, -1 if file
 * could not be opened (not fatal — defaults remain in place).          */
static int parse_config_file(RiskConfig* cfg, const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return -1;   /* file absent — caller keeps defaults */

    char line[1024];
    int  lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char* p = trim_ws(line);

        /* skip blank lines and comments */
        if (*p == '\0' || *p == '#') continue;

        /* split on first '=' */
        char* eq = strchr(p, '=');
        if (!eq) {
            fprintf(stderr, "riskconfig: line %d: no '=' found, skipped: %s\n",
                    lineno, p);
            continue;
        }

        *eq = '\0';
        char* key = trim_ws(p);
        char* val = trim_ws(eq + 1);

        /* upper-case the key for case-insensitive matching */
        char ukey[128];
        safe_strncpy(ukey, key, sizeof(ukey));
        str_to_upper(ukey);

        if (strcmp(ukey, "MAX_POSITION_SIZE") == 0) {
            cfg->max_position_size = atof(val);
        }
        else if (strcmp(ukey, "MIN_POSITION_SIZE") == 0) {
            cfg->min_position_size = atof(val);
        }
        else if (strcmp(ukey, "MAX_ORDER_NOTIONAL") == 0) {
            cfg->max_order_notional = atof(val);
        }
        else if (strcmp(ukey, "MAX_ORDERS_PER_DAY") == 0) {
            cfg->max_orders_per_day = atoi(val);
        }
        else if (strcmp(ukey, "MAX_DRAWDOWN_PCT") == 0) {
            cfg->max_drawdown_pct = atof(val);
        }
        else if (strcmp(ukey, "MAX_DAILY_LOSS") == 0) {
            cfg->max_daily_loss = atof(val);
        }
        else if (strcmp(ukey, "ALLOWED_SYMBOLS") == 0) {
            cfg->allowed_count = parse_symbol_list(
                val, cfg->allowed_symbols, RISK_MAX_SYMBOLS);
            cfg->has_allowed_list = (cfg->allowed_count > 0) ? 1 : 0;
        }
        else if (strcmp(ukey, "BLOCKED_SYMBOLS") == 0) {
            cfg->blocked_count = parse_symbol_list(
                val, cfg->blocked_symbols, RISK_MAX_SYMBOLS);
        }
        else {
            /* forward-compatible: unknown keys are silently ignored */
            fprintf(stderr,
                "riskconfig: line %d: unknown key '%s' (ignored)\n",
                lineno, key);
        }
    }

    fclose(f);
    return 0;
}

void load_risk_config(void) {
    set_defaults(&g_risk_cfg);

    /* resolve path:  $HOME/.las_shell_risk   or  ./.las_shell_risk fallback */
    const char* home = getenv("HOME");
    if (home) {
        snprintf(g_risk_cfg.config_path, sizeof(g_risk_cfg.config_path),
                 "%s%s", home, RISK_CONFIG_PATH);
    } else {
        safe_strncpy(g_risk_cfg.config_path,
                     ".las_shell_risk",
                     sizeof(g_risk_cfg.config_path));
    }

    int rc = parse_config_file(&g_risk_cfg, g_risk_cfg.config_path);
    if (rc == 0) {
        g_risk_cfg.loaded = 1;
        /* only print notification in interactive mode */
        if (isatty(STDIN_FILENO)) {
            fprintf(stderr, "riskconfig: loaded %s\n", g_risk_cfg.config_path);
        }
    } else {
        /* no config file is not an error — defaults are in effect */
        g_risk_cfg.loaded = 0;
    }
    g_risk_loaded = 1;
}

void reload_risk_config(void) {
    /* save path before zero-ing out struct */
    char saved_path[512];
    safe_strncpy(saved_path, g_risk_cfg.config_path, sizeof(saved_path));

    set_defaults(&g_risk_cfg);
    safe_strncpy(g_risk_cfg.config_path, saved_path, sizeof(g_risk_cfg.config_path));

    int rc = parse_config_file(&g_risk_cfg, g_risk_cfg.config_path);
    if (rc == 0) {
        g_risk_cfg.loaded = 1;
        fprintf(stderr, "riskconfig: reloaded %s\n", g_risk_cfg.config_path);
    } else {
        g_risk_cfg.loaded = 0;
        fprintf(stderr,
            "riskconfig: WARNING — %s not found, using built-in defaults\n",
            g_risk_cfg.config_path);
    }
}

const RiskConfig* get_risk_config(void) {
    if (!g_risk_loaded) return NULL;
    return &g_risk_cfg;
}


/* ============================================================
 * parse_order_line
 *   Parses one line:  SYMBOL  ACTION  SIZE  [PRICE]
 *   Returns 1 on success, 0 if the line cannot be parsed
 *   (blank, comment, or malformed).
 * ============================================================ */
static int parse_order_line(const char* line, ParsedOrder* order) {
    if (!line || !order) return 0;

    memset(order, 0, sizeof(ParsedOrder));

    /* skip blank / comment lines */
    const char* p = line;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '\0' || *p == '#') return 0;

    char buf[512];
    safe_strncpy(buf, p, sizeof(buf));

    /* tokenise on whitespace */
    char* saveptr = NULL;
    char* symbol = strtok_r(buf, " \t\r\n", &saveptr);
    if (!symbol) return 0;
    safe_strncpy(order->symbol, symbol, RISK_SYMBOL_LEN);
    str_to_upper(order->symbol);

    char* action = strtok_r(NULL, " \t\r\n", &saveptr);
    if (!action) {
        /* bare symbol line — treat as unquantified, still validate symbol */
        safe_strncpy(order->action, "BUY", sizeof(order->action));
        order->size     = 0.0;
        order->price    = 0.0;
        order->notional = 0.0;
        return 1;
    }
    safe_strncpy(order->action, action, sizeof(order->action));
    str_to_upper(order->action);

    char* size_str = strtok_r(NULL, " \t\r\n", &saveptr);
    if (size_str) {
        order->size = atof(size_str);
    }

    char* price_str = strtok_r(NULL, " \t\r\n", &saveptr);
    if (price_str) {
        order->price = atof(price_str);
    }

    if (order->size > 0.0 && order->price > 0.0) {
        order->notional = order->size * order->price;
    }

    return 1;
}


/* ============================================================
 * symbol_is_allowed  /  symbol_is_blocked
 * ============================================================ */
static int symbol_is_allowed(const RiskConfig* cfg, const char* sym) {
    if (!cfg->has_allowed_list) return 1;   /* no list = all allowed */
    char upper[RISK_SYMBOL_LEN];
    safe_strncpy(upper, sym, sizeof(upper));
    str_to_upper(upper);
    for (int i = 0; i < cfg->allowed_count; i++) {
        if (strcmp(cfg->allowed_symbols[i], upper) == 0) return 1;
    }
    return 0;
}

static int symbol_is_blocked(const RiskConfig* cfg, const char* sym) {
    if (cfg->blocked_count == 0) return 0;
    char upper[RISK_SYMBOL_LEN];
    safe_strncpy(upper, sym, sizeof(upper));
    str_to_upper(upper);
    for (int i = 0; i < cfg->blocked_count; i++) {
        if (strcmp(cfg->blocked_symbols[i], upper) == 0) return 1;
    }
    return 0;
}


/* ============================================================
 * validate_order_against_risk
 *   The main ?> gate check.
 *   Parses the raw order line, then runs every applicable limit.
 *   Stops at first violation (fail-fast, same as assert behaviour).
 *   Returns RISK_PASS(0) or an error code; fills *result.
 * ============================================================ */
int validate_order_against_risk(const char* order_line, RiskResult* result) {
    if (!result) return RISK_ERR_CONFIG;

    /* clear result */
    memset(result, 0, sizeof(RiskResult));
    result->code = RISK_PASS;

    /* no config loaded → always pass  */
    if (!g_risk_loaded) return RISK_PASS;

    const RiskConfig* cfg = &g_risk_cfg;

    ParsedOrder order;
    if (!parse_order_line(order_line, &order)) {
        /* unparseable line — let it through silently */
        return RISK_PASS;
    }

    /* ── 1. Symbol block list check ──────────────────────────── */
    if (symbol_is_blocked(cfg, order.symbol)) {
        result->code = RISK_ERR_SYMBOL;
        snprintf(result->reason,  sizeof(result->reason),
                 "symbol %s is in BLOCKED_SYMBOLS list", order.symbol);
        snprintf(result->field,   sizeof(result->field), "BLOCKED_SYMBOLS");
        result->limit_value  = 0.0;
        result->actual_value = 0.0;
        return RISK_ERR_SYMBOL;
    }

    /* ── 2. Symbol allow list check ──────────────────────────── */
    if (!symbol_is_allowed(cfg, order.symbol)) {
        result->code = RISK_ERR_SYMBOL;
        snprintf(result->reason, sizeof(result->reason),
                 "symbol %s is not in ALLOWED_SYMBOLS list", order.symbol);
        snprintf(result->field,  sizeof(result->field), "ALLOWED_SYMBOLS");
        result->limit_value  = 0.0;
        result->actual_value = 0.0;
        return RISK_ERR_SYMBOL;
    }

    /* ── 3. Minimum position size ────────────────────────────── */
    if (order.size > 0.0 && order.size < cfg->min_position_size) {
        result->code = RISK_ERR_SIZE;
        snprintf(result->reason, sizeof(result->reason),
                 "order size %.0f below MIN_POSITION_SIZE %.0f",
                 order.size, cfg->min_position_size);
        snprintf(result->field,  sizeof(result->field), "MIN_POSITION_SIZE");
        result->limit_value  = cfg->min_position_size;
        result->actual_value = order.size;
        return RISK_ERR_SIZE;
    }

    /* ── 4. Maximum position size ────────────────────────────── */
    if (order.size > 0.0 && order.size > cfg->max_position_size) {
        result->code = RISK_ERR_SIZE;
        snprintf(result->reason, sizeof(result->reason),
                 "order size %.0f exceeds MAX_POSITION_SIZE %.0f",
                 order.size, cfg->max_position_size);
        snprintf(result->field,  sizeof(result->field), "MAX_POSITION_SIZE");
        result->limit_value  = cfg->max_position_size;
        result->actual_value = order.size;
        return RISK_ERR_SIZE;
    }

    /* ── 5. Maximum notional value ───────────────────────────── */
    if (order.notional > 0.0 && order.notional > cfg->max_order_notional) {
        result->code = RISK_ERR_NOTIONAL;
        snprintf(result->reason, sizeof(result->reason),
                 "notional $%.2f exceeds MAX_ORDER_NOTIONAL $%.2f",
                 order.notional, cfg->max_order_notional);
        snprintf(result->field,  sizeof(result->field), "MAX_ORDER_NOTIONAL");
        result->limit_value  = cfg->max_order_notional;
        result->actual_value = order.notional;
        return RISK_ERR_NOTIONAL;
    }

    return RISK_PASS;
}


/* ============================================================
 * assert_risk_limits
 *   Called by command_assert() when invoked with zero operands.
 *   Reads DRAWDOWN and DAILY_LOSS from the environment and
 *   compares against loaded limits.
 * ============================================================ */
int assert_risk_limits(char** env, RiskResult* result) {
    if (!result) return RISK_ERR_CONFIG;
    memset(result, 0, sizeof(RiskResult));
    result->code = RISK_PASS;

    if (!g_risk_loaded || !g_risk_cfg.loaded) {
        snprintf(result->reason, sizeof(result->reason),
                 "no risk config loaded — assert passes by default");
        return RISK_PASS;
    }

    const RiskConfig* cfg = &g_risk_cfg;

    /* ── check DRAWDOWN ──────────────────────────────────────── */
    const char* dd_str = my_getenv("DRAWDOWN", env);
    if (dd_str && *dd_str) {
        double drawdown = atof(dd_str);
        if (drawdown > cfg->max_drawdown_pct) {
            result->code = RISK_ERR_DRAWDOWN;
            snprintf(result->reason, sizeof(result->reason),
                     "$DRAWDOWN %.2f%% exceeds MAX_DRAWDOWN_PCT %.2f%%",
                     drawdown, cfg->max_drawdown_pct);
            snprintf(result->field,  sizeof(result->field), "MAX_DRAWDOWN_PCT");
            result->limit_value  = cfg->max_drawdown_pct;
            result->actual_value = drawdown;
            return RISK_ERR_DRAWDOWN;
        }
    }

    /* ── check DAILY_LOSS ────────────────────────────────────── */
    const char* dl_str = my_getenv("DAILY_LOSS", env);
    if (dl_str && *dl_str) {
        double daily_loss = fabs(atof(dl_str));   /* accept signed or unsigned */
        if (daily_loss > cfg->max_daily_loss) {
            result->code = RISK_ERR_DAILY_LOSS;
            snprintf(result->reason, sizeof(result->reason),
                     "$DAILY_LOSS $%.2f exceeds MAX_DAILY_LOSS $%.2f",
                     daily_loss, cfg->max_daily_loss);
            snprintf(result->field,  sizeof(result->field), "MAX_DAILY_LOSS");
            result->limit_value  = cfg->max_daily_loss;
            result->actual_value = daily_loss;
            return RISK_ERR_DAILY_LOSS;
        }
    }

    return RISK_PASS;
}


/* ============================================================
 * risk_result_print
 * ============================================================ */
void risk_result_print(const RiskResult* r) {
    if (!r) return;
    if (r->code == RISK_PASS) {
        fprintf(stderr, "risk: PASS\n");
        return;
    }
    fprintf(stderr, "risk: REJECTED [%s] %s (limit=%.4g actual=%.4g)\n",
            r->field, r->reason, r->limit_value, r->actual_value);
}


/* ============================================================
 * print_risk_config  —  pretty-print loaded limits
 * ============================================================ */
void print_risk_config(void) {
    const RiskConfig* cfg = &g_risk_cfg;

    if (!g_risk_loaded) {
        printf("Risk config: not loaded\n");
        return;
    }

    printf("\n");
    printf("  ╭─────────────────────────────────────────────────╮\n");
    printf("  │           Las_shell Risk Configuration             │\n");
    if (cfg->loaded) {
        printf("  │  source : %-37s│\n", cfg->config_path);
    } else {
        printf("  │  source : %-37s│\n", "(built-in defaults — no file loaded)");
    }
    printf("  ├─────────────────────────────────────────────────┤\n");
    printf("  │  MAX_POSITION_SIZE   = %-24.0f│\n", cfg->max_position_size);
    printf("  │  MIN_POSITION_SIZE   = %-24.0f│\n", cfg->min_position_size);
    printf("  │  MAX_ORDER_NOTIONAL  = $%-23.2f│\n", cfg->max_order_notional);
    printf("  │  MAX_ORDERS_PER_DAY  = %-24d│\n", cfg->max_orders_per_day);
    printf("  │  MAX_DRAWDOWN_PCT    = %-23.2f%%│\n", cfg->max_drawdown_pct);
    printf("  │  MAX_DAILY_LOSS      = $%-23.2f│\n", cfg->max_daily_loss);
    printf("  ├─────────────────────────────────────────────────┤\n");

    if (cfg->has_allowed_list) {
        printf("  │  ALLOWED_SYMBOLS     = ");
        int printed = 0;
        for (int i = 0; i < cfg->allowed_count; i++) {
            if (i > 0) printf(",");
            printf("%s", cfg->allowed_symbols[i]);
            printed += (int)strlen(cfg->allowed_symbols[i]) + (i > 0 ? 1 : 0);
        }
        /* pad to column width */
        for (int p = printed; p < 24; p++) printf(" ");
        printf("│\n");
    } else {
        printf("  │  ALLOWED_SYMBOLS     = %-24s│\n", "(all symbols permitted)");
    }

    if (cfg->blocked_count > 0) {
        printf("  │  BLOCKED_SYMBOLS     = ");
        int printed = 0;
        for (int i = 0; i < cfg->blocked_count; i++) {
            if (i > 0) printf(",");
            printf("%s", cfg->blocked_symbols[i]);
            printed += (int)strlen(cfg->blocked_symbols[i]) + (i > 0 ? 1 : 0);
        }
        for (int p = printed; p < 24; p++) printf(" ");
        printf("│\n");
    } else {
        printf("  │  BLOCKED_SYMBOLS     = %-24s│\n", "(none)");
    }

    printf("  ╰─────────────────────────────────────────────────╯\n");
    printf("\n");
}


/* ============================================================
 * command_riskconfig  —  built-in: riskconfig [show|reload|path]
 * ============================================================ */
int command_riskconfig(char** args, char** env) {
    (void)env;   /* reserved for future use */

    const char* sub = args[1];   /* may be NULL */

    if (!sub || strcmp(sub, "show") == 0) {
        print_risk_config();
        return 0;
    }

    if (strcmp(sub, "reload") == 0) {
        reload_risk_config();
        print_risk_config();
        return 0;
    }

    if (strcmp(sub, "path") == 0) {
        if (g_risk_loaded) {
            printf("%s\n", g_risk_cfg.config_path);
        } else {
            printf("(not loaded)\n");
        }
        return 0;
    }

    fprintf(stderr,
        "riskconfig: usage: riskconfig [show | reload | path]\n"
        "  show    — display all loaded limits\n"
        "  reload  — re-read ~/.las_shell_risk from disk\n"
        "  path    — print config file path\n");
    return 1;
}
