#include "../include/my_own_shell.h"

static int user_alias_count_ta = -1;

/* Global env pointer — set by setmarket via set_trading_env() */
static char** trading_env = NULL;

void set_trading_env(char** env) {
    trading_env = env;
}

static const char* TA[][2] = {
    {"mstatus",    "cat ~/.las_shell_market"},
    {"mpnl",       "cat ~/.las_shell_pnl"},
    {"mopen",      "echo OPEN 0 > ~/.las_shell_market"},
    {"mclose",     "echo CLOSED 0 > ~/.las_shell_market"},
    {"flatten",    "positions && close_all && pnl"},
    {"check_risk", "python3 $LAS_SHELL_HOME/scripts/risk_check.py"},
    {"risk",       "assert $CAPITAL > 0 && check_risk"},
    {"pnl",        "python3 $LAS_SHELL_HOME/scripts/pnl_report.py"},
    {"pnl_log",    "pnl |> $LAS_SHELL_HOME/logs/pnl.csv"},
    {"gen_orders", "python3 $LAS_SHELL_HOME/scripts/generate_orders.py"},
    {"send_orders","python3 $LAS_SHELL_HOME/scripts/send_orders.py"},
    {"orders",     "gen_orders ?> check_risk && send_orders"},
    {"backtest",   "python3 $LAS_SHELL_HOME/scripts/backtest.py"},
    {"run_strat",  "gen_orders ?> check_risk && send_orders"},
    {"live",       "assert $ACCOUNT == LIVE && run_strat"},
    {"paper",      "assert $ACCOUNT == PAPER && run_strat"},
    {"audit",      "cat $LAS_SHELL_HOME/logs/pnl.csv"},
    {"rejections", "cat ~/.las_shell_risk_rejections"},
    {"morning",    "work && mstatus && positions && risk"},
    {"eod",        "flatten && pnl && mclose && work off"},
    {NULL, NULL}
};

extern int   alias_count;
extern int   add_alias(char* name, char* value);
extern char* find_alias(char* name);
extern int   remove_alias(char* name);

void load_trading_aliases(void) {
    /* Set LAS_SHELL_HOME fallback if not already set */
    char* market = NULL;
    if (trading_env) market = my_getenv("MARKET", trading_env);
    if (!market || market[0] == '\0') market = getenv("MARKET");
    if (!market || market[0] == '\0') return;

    /* PATH FIX: if LAS_SHELL_HOME not set, use /usr/local/share/las_shell */
    char* lhome = getenv("LAS_SHELL_HOME");
    if (!lhome || lhome[0] == '\0') {
        setenv("LAS_SHELL_HOME", "/usr/local/share/las_shell", 1);
    }

    if (user_alias_count_ta < 0)
        user_alias_count_ta = alias_count;

    for (int i = 0; TA[i][0] != NULL; i++) {
        if (!find_alias((char*)TA[i][0]))
            add_alias((char*)TA[i][0], (char*)TA[i][1]);
    }
}

void reload_trading_aliases(void) {
    for (int i = 0; TA[i][0] != NULL; i++)
        remove_alias((char*)TA[i][0]);
    user_alias_count_ta = -1;
    load_trading_aliases();
}