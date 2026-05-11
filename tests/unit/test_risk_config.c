/* ============================================================
 * Las_shell Phase 4.3  —  Risk Config Unit Tests
 * test_risk_config.c
 *
 * Standalone test binary. Does NOT require the rest of Las_shell.
 * Compile:
 *   gcc -Wall -Wextra -g -I.. -I/tmp \
 *       test_risk_config.c ../risk_config.c \
 *       -lm -o test_risk_config
 * Run:
 *   ./test_risk_config
 *
 * Exit 0 = all tests passed.  Non-zero = failure count.
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "risk_config.h"

/* ── Minimal stubs for helpers used transitively ─────────────
 * (my_getenv is used by assert_risk_limits)                   */
char* my_getenv(const char* name, char** env) {
    if (!env || !name) return NULL;
    size_t nlen = strlen(name);
    for (int i = 0; env[i]; i++) {
        if (strncmp(env[i], name, nlen) == 0 && env[i][nlen] == '=')
            return env[i] + nlen + 1;
    }
    return NULL;
}

/* ── Tiny test framework ─────────────────────────────────────  */
static int g_total  = 0;
static int g_passed = 0;
static int g_failed = 0;

#define TEST(name, expr) do {                                         \
    g_total++;                                                        \
    int _ok = !!(expr);                                               \
    if (_ok) { g_passed++; printf("  [PASS] %s\n", (name)); }        \
    else     { g_failed++; printf("  [FAIL] %s\n", (name));          \
               printf("         expr: %s\n", #expr); }               \
} while(0)

#define SECTION(title) printf("\n── %s ──\n", (title))

/* ── Helper: write a temporary config file ──────────────────── */
static char g_tmp_cfg_path[512] = "";

static void write_tmp_config(const char* content) {
    snprintf(g_tmp_cfg_path, sizeof(g_tmp_cfg_path),
             "/tmp/las_shell_risk_test_%d", (int)getpid());
    FILE* f = fopen(g_tmp_cfg_path, "w");
    if (!f) { perror("fopen tmp cfg"); exit(1); }
    fputs(content, f);
    fclose(f);
    /* Point HOME to /tmp so load_risk_config() finds our file at /tmp/.las_shell_risk */
}

static void remove_tmp_config(void) {
    if (g_tmp_cfg_path[0]) unlink(g_tmp_cfg_path);
}

/* ── Test groups ─────────────────────────────────────────────── */

/* ── 1. Config parser ─────────────────────────────────────── */
static void test_config_parser(void) {
    SECTION("1. Config file parsing");

    /* Write a realistic config and load it via the internal path trick */
    const char* cfg_text =
        "# Test risk config\n"
        "MAX_POSITION_SIZE   = 1000\n"
        "MIN_POSITION_SIZE   = 10\n"
        "MAX_DRAWDOWN_PCT    = 5.0\n"
        "MAX_DAILY_LOSS      = 2000\n"
        "MAX_ORDER_NOTIONAL  = 500000\n"
        "MAX_ORDERS_PER_DAY  = 50\n"
        "ALLOWED_SYMBOLS     = SPY,QQQ,IWM,AAPL,MSFT\n"
        "BLOCKED_SYMBOLS     = GME,AMC\n"
        "\n"
        "# Another comment\n";

    /* Write to /tmp/.las_shell_risk and override HOME temporarily */
    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/las_shell_test_%d", (int)getpid());
    mkdir(tmp_dir, 0700);
    char cfg_path[512];
    snprintf(cfg_path, sizeof(cfg_path), "%s/.las_shell_risk", tmp_dir);
    FILE* f = fopen(cfg_path, "w");
    fputs(cfg_text, f);
    fclose(f);

    /* swap HOME */
    char* old_home = getenv("HOME");
    setenv("HOME", tmp_dir, 1);
    load_risk_config();
    if (old_home) setenv("HOME", old_home, 1);
    else          unsetenv("HOME");

    const RiskConfig* cfg = get_risk_config();
    TEST("config loaded",               cfg != NULL);
    TEST("config->loaded == 1",         cfg && cfg->loaded == 1);
    TEST("MAX_POSITION_SIZE = 1000",    cfg && cfg->max_position_size  == 1000.0);
    TEST("MIN_POSITION_SIZE = 10",      cfg && cfg->min_position_size  == 10.0);
    TEST("MAX_DRAWDOWN_PCT  = 5.0",     cfg && cfg->max_drawdown_pct   == 5.0);
    TEST("MAX_DAILY_LOSS    = 2000",    cfg && cfg->max_daily_loss     == 2000.0);
    TEST("MAX_ORDER_NOTIONAL= 500000",  cfg && cfg->max_order_notional == 500000.0);
    TEST("MAX_ORDERS_PER_DAY= 50",      cfg && cfg->max_orders_per_day == 50);
    TEST("has_allowed_list  = 1",       cfg && cfg->has_allowed_list   == 1);
    TEST("allowed_count     = 5",       cfg && cfg->allowed_count      == 5);
    TEST("blocked_count     = 2",       cfg && cfg->blocked_count      == 2);
    TEST("allowed[0] = SPY",            cfg && strcmp(cfg->allowed_symbols[0], "SPY") == 0);
    TEST("allowed[4] = MSFT",           cfg && strcmp(cfg->allowed_symbols[4], "MSFT") == 0);
    TEST("blocked[0] = GME",            cfg && strcmp(cfg->blocked_symbols[0], "GME") == 0);

    /* cleanup */
    unlink(cfg_path);
    rmdir(tmp_dir);
}


/* ── 2. Order line parser  (via validate, pass case) ─────── */
static void test_order_parser(void) {
    SECTION("2. Order line parsing (pass cases)");

    /* With the config loaded above:
     *   ALLOWED: SPY QQQ IWM AAPL MSFT
     *   BLOCKED: GME AMC
     *   MAX_POSITION_SIZE = 1000
     *   MAX_ORDER_NOTIONAL = 500000                              */

    RiskResult rr;

    /* valid order — within all limits */
    int rc = validate_order_against_risk("SPY BUY 100 485.20", &rr);
    TEST("SPY 100@485 -> PASS",         rc == RISK_PASS);

    /* no price supplied — notional check cannot fire */
    rc = validate_order_against_risk("QQQ SELL 500", &rr);
    TEST("QQQ 500 no price -> PASS",    rc == RISK_PASS);

    /* bare symbol — just symbol check */
    rc = validate_order_against_risk("AAPL", &rr);
    TEST("bare symbol AAPL -> PASS",    rc == RISK_PASS);

    /* blank line → pass silently */
    rc = validate_order_against_risk("", &rr);
    TEST("blank line -> PASS",          rc == RISK_PASS);

    /* comment line → pass silently */
    rc = validate_order_against_risk("# this is a comment", &rr);
    TEST("comment line -> PASS",        rc == RISK_PASS);
}


/* ── 3. Symbol enforcement ───────────────────────────────── */
static void test_symbol_enforcement(void) {
    SECTION("3. Symbol allow / block enforcement");

    RiskResult rr;
    int rc;

    /* blocked symbol */
    rc = validate_order_against_risk("GME BUY 100 15.00", &rr);
    TEST("GME (blocked) -> REJECTED",   rc == RISK_ERR_SYMBOL);
    TEST("GME reason mentions BLOCKED", rc == RISK_ERR_SYMBOL &&
                                        strstr(rr.reason, "BLOCKED") != NULL);

    rc = validate_order_against_risk("AMC SELL 50 6.00", &rr);
    TEST("AMC (blocked) -> REJECTED",   rc == RISK_ERR_SYMBOL);

    /* symbol not in allowed list */
    rc = validate_order_against_risk("TSLA BUY 100 200.00", &rr);
    TEST("TSLA (not allowed) -> REJECTED", rc == RISK_ERR_SYMBOL);
    TEST("TSLA reason mentions ALLOWED",   rc == RISK_ERR_SYMBOL &&
                                           strstr(rr.reason, "ALLOWED") != NULL);

    /* case-insensitive: lowercase symbol should still match */
    rc = validate_order_against_risk("spy buy 100 485.00", &rr);
    TEST("lowercase spy -> PASS",       rc == RISK_PASS);
}


/* ── 4. Position size limits ─────────────────────────────── */
static void test_size_limits(void) {
    SECTION("4. Position size limits");

    RiskResult rr;
    int rc;

    /* exactly at max — should PASS */
    rc = validate_order_against_risk("SPY BUY 1000 1.00", &rr);
    TEST("size=1000 (at max) -> PASS",  rc == RISK_PASS);

    /* one over max — should REJECT */
    rc = validate_order_against_risk("SPY BUY 1001 1.00", &rr);
    TEST("size=1001 (over max) -> REJECTED", rc == RISK_ERR_SIZE);
    TEST("size error field = MAX_POSITION_SIZE",
         rc == RISK_ERR_SIZE && strcmp(rr.field, "MAX_POSITION_SIZE") == 0);
    TEST("size actual_value = 1001",    rc == RISK_ERR_SIZE && rr.actual_value == 1001.0);
    TEST("size limit_value  = 1000",    rc == RISK_ERR_SIZE && rr.limit_value  == 1000.0);

    /* below minimum */
    rc = validate_order_against_risk("SPY BUY 5 485.00", &rr);
    TEST("size=5 (below min=10) -> REJECTED", rc == RISK_ERR_SIZE);
    TEST("size error field = MIN_POSITION_SIZE",
         rc == RISK_ERR_SIZE && strcmp(rr.field, "MIN_POSITION_SIZE") == 0);

    /* exactly at min — should PASS */
    rc = validate_order_against_risk("SPY BUY 10 1.00", &rr);
    TEST("size=10 (at min) -> PASS",    rc == RISK_PASS);
}


/* ── 5. Notional limits ──────────────────────────────────── */
static void test_notional_limits(void) {
    SECTION("5. Notional value limits");

    RiskResult rr;
    int rc;

    /* 1000 shares * $490 = $490,000  < $500,000 limit */
    rc = validate_order_against_risk("SPY BUY 1000 490.00", &rr);
    TEST("notional 490k (under 500k) -> PASS",  rc == RISK_PASS);

    /* 1000 * $510 = $510,000 > $500,000 */
    rc = validate_order_against_risk("SPY BUY 1000 510.00", &rr);
    TEST("notional 510k (over 500k) -> REJECTED", rc == RISK_ERR_NOTIONAL);
    TEST("notional field = MAX_ORDER_NOTIONAL",
         rc == RISK_ERR_NOTIONAL &&
         strcmp(rr.field, "MAX_ORDER_NOTIONAL") == 0);
    TEST("notional actual ~510000",
         rc == RISK_ERR_NOTIONAL && rr.actual_value > 509999.0);

    /* no price = no notional check */
    rc = validate_order_against_risk("SPY BUY 1000", &rr);
    TEST("no price -> no notional check -> PASS", rc == RISK_PASS);
}


/* ── 6. assert_risk_limits (env-based) ───────────────────── */
static void test_assert_risk_limits(void) {
    SECTION("6. assert_risk_limits() — env-based checks");

    RiskResult rr;
    int rc;

    /* build a minimal env */
    char* env_ok[] = {
        "DRAWDOWN=2.5",
        "DAILY_LOSS=500.0",
        NULL
    };
    rc = assert_risk_limits(env_ok, &rr);
    TEST("DRAWDOWN=2.5, DAILY_LOSS=500 -> PASS", rc == RISK_PASS);

    /* drawdown exceeds 5% limit */
    char* env_dd[] = {
        "DRAWDOWN=6.0",
        "DAILY_LOSS=100.0",
        NULL
    };
    rc = assert_risk_limits(env_dd, &rr);
    TEST("DRAWDOWN=6.0 (over 5%) -> REJECTED",  rc == RISK_ERR_DRAWDOWN);
    TEST("drawdown field = MAX_DRAWDOWN_PCT",
         rc == RISK_ERR_DRAWDOWN &&
         strcmp(rr.field, "MAX_DRAWDOWN_PCT") == 0);

    /* daily loss exceeds $2000 limit */
    char* env_dl[] = {
        "DRAWDOWN=1.0",
        "DAILY_LOSS=2500.0",
        NULL
    };
    rc = assert_risk_limits(env_dl, &rr);
    TEST("DAILY_LOSS=2500 (over 2000) -> REJECTED", rc == RISK_ERR_DAILY_LOSS);

    /* signed daily loss (negative PnL) */
    char* env_neg[] = {
        "DRAWDOWN=1.0",
        "DAILY_LOSS=-3000.0",
        NULL
    };
    rc = assert_risk_limits(env_neg, &rr);
    TEST("DAILY_LOSS=-3000 (abs > 2000) -> REJECTED", rc == RISK_ERR_DAILY_LOSS);

    /* missing env vars — should pass gracefully */
    char* env_empty[] = { NULL };
    rc = assert_risk_limits(env_empty, &rr);
    TEST("empty env -> PASS (no vars to check)", rc == RISK_PASS);
}


/* ── 7. No-config defaults ───────────────────────────────── */
static void test_no_config(void) {
    SECTION("7. Behaviour with no config file");

    /* point HOME to a dir with no .las_shell_risk */
    char* old_home = getenv("HOME");
    setenv("HOME", "/tmp", 1);
    load_risk_config();   /* should load defaults, not fail */
    if (old_home) setenv("HOME", old_home, 1);

    const RiskConfig* cfg = get_risk_config();
    TEST("get_risk_config() not NULL after no-file load", cfg != NULL);
    /* With no file, loaded==0 but singleton is initialised with defaults */
    TEST("has_allowed_list = 0 (all symbols permitted)", cfg && cfg->has_allowed_list == 0);

    /* An order to any symbol should pass when there's no allowed list */
    RiskResult rr;
    int rc = validate_order_against_risk("TSLA BUY 100 200.00", &rr);
    TEST("TSLA passes when no allowed list loaded", rc == RISK_PASS);

    /* Reload the real test config for subsequent tests */
}


/* ── 8. Edge cases ──────────────────────────────────────── */
static void test_edge_cases(void) {
    SECTION("8. Edge cases and robustness");

    /* Reload a config with no allowed list */
    const char* cfg_text =
        "MAX_POSITION_SIZE   = 500\n"
        "MAX_DRAWDOWN_PCT    = 3.0\n"
        "MAX_DAILY_LOSS      = 1000\n"
        "MAX_ORDER_NOTIONAL  = 100000\n";

    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/las_shell_edge_%d", (int)getpid());
    mkdir(tmp_dir, 0700);
    char cfg_path[512];
    snprintf(cfg_path, sizeof(cfg_path), "%s/.las_shell_risk", tmp_dir);
    FILE* f = fopen(cfg_path, "w");
    fputs(cfg_text, f);
    fclose(f);
    char* old_home = getenv("HOME");
    setenv("HOME", tmp_dir, 1);
    load_risk_config();
    if (old_home) setenv("HOME", old_home, 1);

    RiskResult rr;
    int rc;

    /* NULL order line → pass */
    rc = validate_order_against_risk(NULL, &rr);
    TEST("NULL order line -> PASS",     rc == RISK_PASS);

    /* whitespace-only line → pass */
    rc = validate_order_against_risk("   \t  ", &rr);
    TEST("whitespace-only -> PASS",     rc == RISK_PASS);

    /* no allowed list → all symbols OK */
    rc = validate_order_against_risk("NVDA BUY 100 400.00", &rr);
    TEST("NVDA with no allowed list -> PASS", rc == RISK_PASS);

    /* size exactly at new max=500 */
    rc = validate_order_against_risk("NVDA BUY 500 1.00", &rr);
    TEST("size=500 at new max=500 -> PASS", rc == RISK_PASS);

    /* size 501 over new max=500 */
    rc = validate_order_against_risk("NVDA BUY 501 1.00", &rr);
    TEST("size=501 over new max=500 -> REJECTED", rc == RISK_ERR_SIZE);

    /* notional: 100 * $1001 = $100100 > $100000 */
    rc = validate_order_against_risk("NVDA BUY 100 1001.00", &rr);
    TEST("notional 100100 > 100000 -> REJECTED", rc == RISK_ERR_NOTIONAL);

    /* zero size — should pass (not an order, harmless) */
    rc = validate_order_against_risk("NVDA BUY 0 400.00", &rr);
    TEST("size=0 -> PASS (no size check on zero)", rc == RISK_PASS);

    /* NULL result pointer — should not segfault */
    rc = validate_order_against_risk("NVDA BUY 100 400.00", NULL);
    TEST("NULL result ptr -> returns ERR_CONFIG without crash",
         rc == RISK_ERR_CONFIG);

    /* cleanup */
    unlink(cfg_path);
    rmdir(tmp_dir);
}


/* ── 9. reload_risk_config ────────────────────────────────── */
static void test_reload(void) {
    SECTION("9. reload_risk_config()");

    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/las_shell_reload_%d", (int)getpid());
    mkdir(tmp_dir, 0700);
    char cfg_path[512];
    snprintf(cfg_path, sizeof(cfg_path), "%s/.las_shell_risk", tmp_dir);

    /* initial config */
    FILE* f = fopen(cfg_path, "w");
    fputs("MAX_POSITION_SIZE = 200\n", f);
    fclose(f);

    char* old_home = getenv("HOME");
    setenv("HOME", tmp_dir, 1);
    load_risk_config();

    const RiskConfig* cfg = get_risk_config();
    TEST("initial MAX_POSITION_SIZE = 200", cfg && cfg->max_position_size == 200.0);

    /* update config on disk */
    f = fopen(cfg_path, "w");
    fputs("MAX_POSITION_SIZE = 750\n", f);
    fclose(f);

    reload_risk_config();
    cfg = get_risk_config();
    TEST("after reload MAX_POSITION_SIZE = 750", cfg && cfg->max_position_size == 750.0);

    if (old_home) setenv("HOME", old_home, 1);
    unlink(cfg_path);
    rmdir(tmp_dir);
}


/* ── main ────────────────────────────────────────────────── */
int main(void) {
    printf("\n╭────────────────────────────────────────────────────────╮\n");
    printf("│   Las_shell Phase 4.3  —  Risk Config Test Suite          │\n");
    printf("╰────────────────────────────────────────────────────────╯\n");

    test_config_parser();
    test_order_parser();
    test_symbol_enforcement();
    test_size_limits();
    test_notional_limits();
    test_assert_risk_limits();
    test_no_config();
    test_edge_cases();
    test_reload();

    printf("\n╭────────────────────────────────────────────────────────╮\n");
    printf("│  Results:  %3d passed  /  %3d failed  /  %3d total     │\n",
           g_passed, g_failed, g_total);
    printf("╰────────────────────────────────────────────────────────╯\n\n");

    return g_failed;
}
