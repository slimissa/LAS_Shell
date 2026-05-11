/*
 * Las_shell Pipeline Stage 5 — execute.c  (C implementation)
 * =========================================================
 * SINK stage: receives sized candidates, sends orders to broker
 * (or simulates in paper mode), and writes execution receipts as
 * a JSON array to stdout for audit chaining via |> operator.
 *
 * Compile:  gcc -Wall -O2 -o execute execute.c -lm
 * Usage:    ... | ./execute
 *           ... | ./execute --mode paper
 *           ... | ./execute --mode paper --slippage_bps 2
 *           ... | ./execute --dry_run
 *           ... | ./execute --mode paper |> logs/executions.csv
 *
 * Safety: LIVE mode is blocked unless LAS_SHELL_LIVE_CONFIRMED=1 is set.
 *
 * stdin  : JSON array of sized candidates (convention v1.0)
 * stdout : JSON array of execution receipts (for audit chaining)
 * stderr : per-order fill confirmations and diagnostics
 * exit 0 : all orders sent (or simulated) successfully
 * exit 1 : any order failed or unsized candidate detected
 *
 * Participates in Las_shell pipelines with zero changes to pipes.c.
 * Fully interchangeable with execute.py.
 *
 * For LIVE order routing, replace execute_order_paper() with a
 * libcurl call to your broker REST API (see Phase 4: broker.c).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MAX_CANDS       64
#define SYM_LEN         16
#define BUFLEN          131072
#define DEFAULT_SLIP    2       /* default slippage in bps */

typedef struct {
    char   symbol[SYM_LEN];
    double price;
    double signal;
    int    size;
    char   side[8];
    /* Receipt fields */
    double fill_price;
    double notional;
    int    ok;           /* 1 = filled, 0 = error */
    char   status[16];   /* "FILLED" / "DRY_RUN" / "ERROR" */
} Candidate;

/* ── Parse helpers ────────────────────────────────────────────── */
static double extract_double(const char *buf, const char *key) {
    char search[32];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(buf, search);
    if (!p) return 0.0;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    return atof(p);
}

static int extract_int(const char *buf, const char *key) {
    return (int)extract_double(buf, key);
}

/* ── Slippage model ───────────────────────────────────────────── */
static double apply_slippage(double price, const char *side, int slip_bps) {
    double slip = price * slip_bps / 10000.0;
    return !strcmp(side, "BUY") ? price + slip : price - slip;
}

/* ── Paper execution (no network call) ───────────────────────── */
static void execute_order_paper(Candidate *c, int slip_bps) {
    c->fill_price = round(apply_slippage(c->price, c->side, slip_bps) * 100.0) / 100.0;
    c->notional   = round(c->fill_price * c->size * 100.0) / 100.0;
    c->ok         = 1;
    strcpy(c->status, "FILLED");
}

/* ── Print one execution receipt as JSON ─────────────────────── */
static void print_receipt(const Candidate *c, const char *mode,
                           int slip_bps, int first) {
    char ts[32];
    time_t now = time(NULL);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    if (!first) printf(",\n");
    printf("  {\n");
    printf("    \"symbol\":          \"%s\",\n",   c->symbol);
    printf("    \"side\":            \"%s\",\n",   c->side);
    printf("    \"size\":            %d,\n",        c->size);
    printf("    \"requested_price\": %.2f,\n",      c->price);
    printf("    \"fill_price\":      %.2f,\n",      c->fill_price);
    printf("    \"notional\":        %.2f,\n",      c->notional);
    printf("    \"slippage_bps\":    %d,\n",        slip_bps);
    printf("    \"mode\":            \"%s\",\n",    mode);
    printf("    \"status\":          \"%s\",\n",    c->status);
    printf("    \"timestamp\":       \"%s\",\n",    ts);
    printf("    \"meta\": {\n");
    printf("      \"_convention\": \"1.0\",\n");
    printf("      \"stage\":       \"execute\",\n");
    printf("      \"language\":    \"c\"\n");
    printf("    }\n");
    printf("  }");
}

int main(int argc, char **argv) {
    /* ── Parse arguments ─────────────────────────────────────── */
    char *acct_env  = getenv("ACCOUNT");
    int   live_mode = acct_env && !strcmp(acct_env, "LIVE");
        char  mode[16];
    if (acct_env && acct_env[0]) {
        strncpy(mode, acct_env, 15);
        mode[15] = '\0';
    } else {
        strcpy(mode, "paper");
    }
    int   slip_bps  = DEFAULT_SLIP;
    int   dry_run   = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--mode") && i+1 < argc) {
            strncpy(mode, argv[++i], 15); mode[15] = '\0';
            live_mode = !strcmp(mode, "live") || !strcmp(mode, "LIVE");
        }
        if (!strcmp(argv[i], "--slippage_bps") && i+1 < argc)
            slip_bps = atoi(argv[++i]);
        if (!strcmp(argv[i], "--dry_run"))
            dry_run = 1;
    }

    /* ── Safety gate: block LIVE without confirmation ────────── */
    if (live_mode && !getenv("LAS_SHELL_LIVE_CONFIRMED")) {
        fprintf(stderr,
            "[execute/c] ERROR: LIVE mode requires "
            "LAS_SHELL_LIVE_CONFIRMED=1 env var\n");
        return 1;
    }
    if (live_mode) strcpy(mode, "live");

    /* ── Read stdin ──────────────────────────────────────────── */
    char *buf = malloc(BUFLEN);
    if (!buf) { fprintf(stderr, "[execute/c] malloc failed\n"); return 1; }
    size_t n = fread(buf, 1, BUFLEN - 1, stdin);
    buf[n] = '\0';

    if (n == 0 || !strcmp(buf, "[]")) {
        printf("[]\n");
        fprintf(stderr, "[execute/c] no orders to execute\n");
        free(buf); return 0;
    }

    /* ── Parse candidates ────────────────────────────────────── */
    Candidate cands[MAX_CANDS];
    int n_cands = 0;
    const char *p = buf;

    while ((p = strstr(p, "\"symbol\"")) != NULL && n_cands < MAX_CANDS) {
        Candidate c = {0};
        p += 8;
        while (*p && *p != '"') p++;
        if (!*p) break;
        p++;
        int i = 0;
        while (*p && *p != '"' && i < SYM_LEN - 1) c.symbol[i++] = *p++;

        /* Extract object scope for field parsing */
        const char *obj_start = p;
        int depth = 0;
        const char *scan = p;
        while (*scan) {
            if (*scan == '{') depth++;
            if (*scan == '}') { if (depth == 0) break; depth--; }
            scan++;
        }
        size_t obj_len = (size_t)(scan - obj_start);
        char obj[2048] = {0};
        if (obj_len >= sizeof(obj)) obj_len = sizeof(obj) - 1;
        strncpy(obj, obj_start, obj_len);

        c.price  = extract_double(obj, "price");
        c.signal = extract_double(obj, "signal");
        c.size   = extract_int(obj, "size");
        if (c.price <= 0.0) c.price = 100.0;

        const char *side_p = strstr(obj, "\"side\"");
        if (side_p) {
            side_p += 6;
            while (*side_p && *side_p != '"') side_p++;
            if (*side_p) {
                side_p++;
                int si = 0;
                while (*side_p && *side_p != '"' && si < 7) c.side[si++] = *side_p++;
                c.side[si] = '\0';
            }
        }
        if (!c.side[0]) strcpy(c.side, "BUY");

        cands[n_cands++] = c;
    }

    /* ── Validate all candidates are sized ───────────────────── */
    int all_ok = 1;
    for (int i = 0; i < n_cands; i++) {
        if (cands[i].size == 0) {
            fprintf(stderr,
                "[execute/c] ERROR: %s has size=0 — "
                "run size_positions before execute\n",
                cands[i].symbol);
            all_ok = 0;
        }
    }
    if (!all_ok) { free(buf); return 1; }

    /* ── Execute orders ──────────────────────────────────────── */
    int receipts = 0;
    double total_notional = 0.0;

    printf("[\n");
    for (int i = 0; i < n_cands; i++) {
        Candidate *c = &cands[i];

        if (dry_run) {
            c->fill_price = c->price;
            c->notional   = c->price * c->size;
            c->ok         = 1;
            strcpy(c->status, "DRY_RUN");
            fprintf(stderr,
                "[execute/c] DRY_RUN %s %d %s @ $%.2f  "
                "notional=$%.0f\n",
                c->side, c->size, c->symbol, c->price, c->notional);
        } else {
            /* Paper mode: local simulation.
             * Replace with libcurl broker call for live mode. */
            execute_order_paper(c, slip_bps);
            fprintf(stderr,
                "[execute/c] FILLED %s %d %s @ $%.2f  "
                "notional=$%.0f  mode=%s\n",
                c->side, c->size, c->symbol, c->fill_price,
                c->notional, mode);
        }

        print_receipt(c, mode, slip_bps, i == 0);
        total_notional += c->notional;
        receipts++;
    }
    printf("\n]\n");

    fprintf(stderr,
            "[execute/c] %d orders %s | total_notional=$%.0f\n",
            receipts, mode, total_notional);

    free(buf);
    return all_ok ? 0 : 1;
}
