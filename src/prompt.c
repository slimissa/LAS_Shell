#include "../include/my_own_shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>

/* ── ANSI colour codes ── */
#define COLOR_RESET   "\001\033[0m\002"
#define COLOR_RED     "\001\033[31m\002"
#define COLOR_GREEN   "\001\033[32m\002"
#define COLOR_YELLOW  "\001\033[33m\002"
#define COLOR_BLUE    "\001\033[34m\002"
#define COLOR_BOLD    "\001\033[1m\002"
#define COLOR_ORANGE  "\001\033[38;5;214m\002"

/* ── Trading status files ──
 * ~/.las_shell_market : "OPEN 47" / "CLOSED 387" / "PRE 12" / "AFTER 23"
 * ~/.las_shell_pnl    : "+1240.50" or "-320.00"                           */
#define MARKET_FILE "/.las_shell_market"
#define PNL_FILE    "/.las_shell_pnl"

/* Global prompt state */
static int   last_exit_status = 0;
static int   work_mode        = 0;   /* 0=normal prompt, 1=trading prompt */
static int watch_stop = 0;
static char* current_user     = NULL;
static char  hostname[64]     = "";

/* ── Init ── */
void init_prompt_info() {
    current_user = getenv("USER");
    if (!current_user) current_user = getenv("LOGNAME");
    if (!current_user) current_user = "user";

    if (gethostname(hostname, sizeof(hostname)) == -1) {
        strcpy(hostname, "localhost");
    } else {
        char* dot = strchr(hostname, '.');
        if (dot) *dot = '\0';
    }
}

void update_exit_status(int status) {
    last_exit_status = status;
}

/* Toggle trading prompt on/off */
void set_work_mode(int on) {
    work_mode = on;
}

int get_work_mode(void) {
    return work_mode;
}

/* ── File helpers ── */
static int read_home_file(const char* filename, char* buf, size_t buflen) {
    char* home = getenv("HOME");
    if (!home) return 0;
    char path[512];
    snprintf(path, sizeof(path), "%s%s", home, filename);
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(buf, (int)buflen, f)) { fclose(f); buf[0] = '\0'; return 0; }
    fclose(f);
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
    return 1;
}

static int get_market_status(char* status, int* minutes) {
    char raw[64] = "";
    strcpy(status, "CLOSED");
    *minutes = 0;
    if (!read_home_file(MARKET_FILE, raw, 64)) return 0;
    sscanf(raw, "%15s %d", status, minutes);
    return 1;
}

/* ── Badge builders ── */
static void build_market_badge(char* out, size_t outlen) {
    char exchange[64] = "NYSE";
    char* menv = getenv("MARKET");
    if (menv && menv[0]) snprintf(exchange, sizeof(exchange), "%s", menv);

    char status[16]; int minutes;
    if (!get_market_status(status, &minutes)) {
        snprintf(out, outlen, "[%s: --]", exchange);
        return;
    }
    int h = minutes / 60;
    int m = minutes % 60;
    snprintf(out, outlen, "[%s: %s +%02d:%02d]", exchange, status, h, m);
}

static void build_pnl_badge(char* out, size_t outlen) {
    char raw[64] = "";
    if (!read_home_file(PNL_FILE, raw, sizeof(raw))) {
        snprintf(out, outlen, "[P&L: --]");
        return;
    }
    double pnl = atof(raw);
    if (pnl >= 0)
        snprintf(out, outlen, "[P&L: +$%.0f]", pnl);
    else
        snprintf(out, outlen, "[P&L: -$%.0f]", -pnl);
}

/* Badge colour:
 *   OPEN  + P&L >= 0  → GREEN
 *   OPEN  + P&L <  0  → YELLOW
 *   PRE                → ORANGE
 *   CLOSED / AFTER     → RED            */
static const char* badge_color(void) {
    char status[16]; int minutes;
    if (!get_market_status(status, &minutes)) return COLOR_RED;

    if (strcmp(status, "PRE") == 0)   return COLOR_ORANGE;
    if (strcmp(status, "AFTER") == 0) return COLOR_RED;
    if (strcmp(status, "CLOSED") == 0) return COLOR_RED;

    /* OPEN — colour depends on P&L */
    char praw[64] = "";
    read_home_file(PNL_FILE, praw, sizeof(praw));
    double pnl = atof(praw);
    return (pnl >= 0) ? COLOR_GREEN : COLOR_YELLOW;
}

/* ── Prompt ── */
char* generate_prompt() {
    static char prompt[1024];
    char cwd[256];
    char user_host[128];

    /* cwd with ~ substitution */
    if (getcwd(cwd, sizeof(cwd)) == NULL) strcpy(cwd, "?");
    char* home = getenv("HOME");
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        char tmp[256];
        snprintf(tmp, sizeof(tmp), "~%s", cwd + strlen(home));
        strcpy(cwd, tmp);
    }
    snprintf(user_host, sizeof(user_host), "%s@%s", current_user, hostname);

    const char* sc = (last_exit_status == 0) ? COLOR_GREEN : COLOR_RED;

    if (!work_mode) {
        /* ── Normal prompt ──────────────────────────────────────
         * ╭─slim@slim ~/Documents/DIY_Shell
         * ╰─$                                                    */
        snprintf(prompt, sizeof(prompt),
            "%s╭─%s%s%s %s%s%s\n"
            "%s╰─%s$ %s",
            COLOR_BOLD,
            COLOR_GREEN, user_host, COLOR_RESET,
            COLOR_BLUE,  cwd,       COLOR_RESET,
            COLOR_BOLD,  sc,        COLOR_RESET
        );
    } else {
        /* ── Trading prompt ─────────────────────────────────────
         * ╭─slim@slim [NYSE: OPEN +00:47] [P&L: +$1240] ~/DIY_Shell
         * ╰─$                                                    */
        char mbadge[128];
        char pbadge[64];
        build_market_badge(mbadge, sizeof(mbadge));
        build_pnl_badge(pbadge,    sizeof(pbadge));
        const char* bc = badge_color();

        snprintf(prompt, sizeof(prompt),
            "%s╭─%s%s%s %s%s%s %s%s%s %s%s%s\n"
            "%s╰─%s$ %s",
            COLOR_BOLD,
            COLOR_GREEN, user_host, COLOR_RESET,
            bc,          mbadge,    COLOR_RESET,
            bc,          pbadge,    COLOR_RESET,
            COLOR_BLUE,  cwd,       COLOR_RESET,
            COLOR_BOLD,  sc,        COLOR_RESET
        );
    }

    return prompt;
}

void set_watch_stop(int val) { watch_stop = val; }
int  get_watch_stop(void)    { return watch_stop; }

int get_last_exit_status() { return last_exit_status; }