/*
 * Las_shell Pipeline Stage 4 — size_positions.c  (C implementation)
 * ================================================================
 * ENRICHER stage: computes position sizes based on capital allocation
 * and sets the `size` field on every candidate. Never filters —
 * all candidates pass through regardless of their signal value.
 *
 * Allocation models:
 *   equal   : split capital equally across all candidates (default)
 *   signal  : weight proportional to |signal| value
 *   fixed   : fixed dollar amount per position (--amount N)
 *
 * Compile:  gcc -Wall -O2 -o size_positions size_positions.c -lm
 * Usage:    ... | ./size_positions
 *           ... | ./size_positions --model signal
 *           ... | ./size_positions --model fixed --amount 10000
 *           CAPITAL=250000 ./size_positions --model equal
 *
 * stdin  : JSON array (convention v1.0) — candidates must have price set
 * stdout : same JSON array with size field populated
 * stderr : diagnostics only
 *
 * Participates in Las_shell pipelines with zero changes to pipes.c.
 * Fully interchangeable with size_positions.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MAX_CANDS  64
#define SYM_LEN    16
#define BUFLEN     131072
#define MAX_PCT    0.20     /* hard cap: no single position > 20% of capital */

typedef struct {
    char   symbol[SYM_LEN];
    double price;
    double signal;
    char   side[8];
    double weight;   /* allocation weight [0, 1] */
    int    size;     /* computed shares */
} Candidate;

/* ── Parse a double after "key": in a JSON snippet ──────────── */
static double extract_double(const char *buf, const char *key) {
    char search[32];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(buf, search);
    if (!p) return 0.0;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    return atof(p);
}

/* ── Print one sized candidate as JSON ───────────────────────── */
static void print_candidate(const Candidate *c, double capital, int first) {
    char ts[32];
    time_t now = time(NULL);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    if (!first) printf(",\n");
    printf("  {\n");
    printf("    \"symbol\": \"%s\",\n",   c->symbol);
    printf("    \"signal\": %.4f,\n",      c->signal);
    printf("    \"size\":   %d,\n",        c->size);
    printf("    \"price\":  %.2f,\n",      c->price);
    printf("    \"side\":   \"%s\",\n",    c->side);
    printf("    \"meta\": {\n");
    printf("      \"_convention\": \"1.0\",\n");
    printf("      \"stage\":       \"size_positions\",\n");
    printf("      \"language\":    \"c\",\n");
    printf("      \"capital\":     %.2f,\n",   capital);
    printf("      \"allocation\":  %.2f,\n",   capital * c->weight);
    printf("      \"weight\":      %.4f,\n",   c->weight);
    printf("      \"timestamp\":   \"%s\"\n",  ts);
    printf("    }\n");
    printf("  }");
}

int main(int argc, char **argv) {
    /* ── Parse arguments ─────────────────────────────────────── */
    char  *cap_env  = getenv("CAPITAL");
    double capital  = cap_env ? atof(cap_env) : 100000.0;
    char   model[16] = "equal";
    double amount   = 0.0;   /* for fixed model */
    double max_pct  = MAX_PCT;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--capital")  && i+1 < argc) capital  = atof(argv[++i]);
        if (!strcmp(argv[i], "--model")    && i+1 < argc) { strncpy(model, argv[++i], 15); model[15]='\0'; }
        if (!strcmp(argv[i], "--amount")   && i+1 < argc) amount   = atof(argv[++i]);
        if (!strcmp(argv[i], "--max_pct")  && i+1 < argc) max_pct  = atof(argv[++i]);
    }

    /* ── Read stdin ──────────────────────────────────────────── */
    char *buf = malloc(BUFLEN);
    if (!buf) { fprintf(stderr, "[size_positions/c] malloc failed\n"); return 1; }
    size_t n = fread(buf, 1, BUFLEN - 1, stdin);
    buf[n] = '\0';

    if (n == 0 || !strcmp(buf, "[]")) {
        printf("[]\n");
        fprintf(stderr, "[size_positions/c] empty input\n");
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

        /* Scan forward to find the closing brace of this object.
         * We track meta depth to handle the nested meta object.   */
        const char *obj_start = p;
        int depth = 0;
        const char *scan = p;
        while (*scan) {
            if (*scan == '{') depth++;
            if (*scan == '}') { if (depth == 0) break; depth--; }
            scan++;
        }
        size_t obj_len = (size_t)(scan - obj_start);
        char obj[1024] = {0};
        if (obj_len >= sizeof(obj)) obj_len = sizeof(obj) - 1;
        strncpy(obj, obj_start, obj_len);

        c.price  = extract_double(obj, "price");
        c.signal = extract_double(obj, "signal");
        if (c.price <= 0.0) c.price = 100.0;

        /* Preserve side from input */
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
        if (!c.side[0]) strcpy(c.side, c.signal >= 0 ? "BUY" : "SELL");

        cands[n_cands++] = c;
    }

    if (n_cands == 0) {
        printf("[]\n");
        fprintf(stderr, "[size_positions/c] no candidates parsed\n");
        free(buf); return 0;
    }

    /* ── Compute allocation weights ──────────────────────────── */
    if (!strcmp(model, "equal")) {
        double w = 1.0 / n_cands;
        if (w > max_pct) w = max_pct;
        for (int i = 0; i < n_cands; i++) cands[i].weight = w;

    } else if (!strcmp(model, "signal")) {
        double total = 0.0;
        for (int i = 0; i < n_cands; i++) total += fabs(cands[i].signal);
        if (total <= 0.0) total = 1.0;
        for (int i = 0; i < n_cands; i++) {
            double w = fabs(cands[i].signal) / total;
            cands[i].weight = w > max_pct ? max_pct : w;
        }

    } else if (!strcmp(model, "fixed")) {
        double dollar = amount > 0.0 ? amount : capital * max_pct;
        for (int i = 0; i < n_cands; i++)
            cands[i].weight = dollar / capital;

    } else {
        fprintf(stderr, "[size_positions/c] unknown model: %s\n", model);
        free(buf); return 1;
    }

    /* ── Set size on each candidate ──────────────────────────── */
    double total_notional = 0.0;
    for (int i = 0; i < n_cands; i++) {
        double alloc   = capital * cands[i].weight;
        int    size    = (int)(alloc / cands[i].price);
        if (size < 1) size = 1;
        cands[i].size  = size;
        total_notional += size * cands[i].price;
    }

    /* ── Emit JSON array ─────────────────────────────────────── */
    printf("[\n");
    for (int i = 0; i < n_cands; i++)
        print_candidate(&cands[i], capital, i == 0);
    printf("\n]\n");

    fprintf(stderr,
            "[size_positions/c] %d positions sized | model=%s | "
            "capital=$%.0f | total_notional=$%.0f\n",
            n_cands, model, capital, total_notional);

    free(buf);
    return 0;
}
