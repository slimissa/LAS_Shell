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
    char   meta_raw[512]; /* raw input meta object content, "" if none */
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

/* FIX CS4: same lookup, but also reports whether the key was found, so
 * a missing field can be told apart from a genuine value of 0.0. */
static double extract_double_present(const char *buf, const char *key, int *present) {
    char search[32];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(buf, search);
    if (!p) { *present = 0; return 0.0; }
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    *present = 1;
    return atof(p);
}

/* ── Print one sized candidate as JSON ───────────────────────── */
/* FIX CS3: forward unknown meta fields from the input instead of
 * discarding them (same scanner as CM3/CR2, duplicated per translation
 * unit since these pipeline stages share no common library). */
static void print_passthrough_meta_fields(const char *meta_raw, const char **reserved, int n_reserved) {
    if (!meta_raw || !meta_raw[0]) return;
    const char *p = meta_raw;
    while ((p = strchr(p, '"')) != NULL) {
        p++;
        const char *key_start = p;
        const char *key_end = strchr(p, '"');
        if (!key_end) break;
        char key[64] = {0};
        size_t klen = (size_t)(key_end - key_start);
        if (klen >= sizeof(key)) klen = sizeof(key) - 1;
        strncpy(key, key_start, klen);

        int skip = 0;
        for (int i = 0; i < n_reserved; i++) if (!strcmp(key, reserved[i])) { skip = 1; break; }

        const char *colon = strchr(key_end, ':');
        if (!colon) break;
        const char *vp = colon + 1;
        while (*vp == ' ') vp++;
        const char *val_start = vp, *val_end;
        int is_str = (*vp == '"');
        if (is_str) { vp++; val_start = vp; val_end = strchr(vp, '"'); if (!val_end) break; }
        else        { val_end = vp; while (*val_end && *val_end != ',' && *val_end != '}') val_end++; }

        if (!skip) {
            char val[256] = {0};
            size_t vlen = (size_t)(val_end - val_start);
            if (vlen >= sizeof(val)) vlen = sizeof(val) - 1;
            strncpy(val, val_start, vlen);
            if (is_str) printf(",\n      \"%s\": \"%s\"", key, val);
            else        printf(",\n      \"%s\": %s", key, val);
        }
        p = is_str ? val_end + 1 : val_end;
    }
}

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
    printf("      \"timestamp\":   \"%s\"",  ts);
    static const char *reserved[] = {"_convention", "stage", "language", "capital",
                                      "allocation", "weight", "timestamp"};
    print_passthrough_meta_fields(c->meta_raw, reserved, 7);
    printf("\n    }\n");
    printf("  }");
}

int main(int argc, char **argv) {
    /* ── Parse arguments ─────────────────────────────────────── */
    char  *cap_env  = getenv("CAPITAL");
    double capital  = cap_env ? atof(cap_env) : 100000.0;
    char   model[16] = "equal";
    double amount   = 0.0;   /* for fixed model */
    double max_pct  = MAX_PCT;
    /* FIX CR5: same limits risk_filter.c used to (unreachably) check --
     * see the comment in risk_filter.c's check() for why they moved here. */
    double max_notional = 500000.0;
    int    max_size      = 5000;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--capital")  && i+1 < argc) capital  = atof(argv[++i]);
        if (!strcmp(argv[i], "--model")    && i+1 < argc) { strncpy(model, argv[++i], 15); model[15]='\0'; }
        if (!strcmp(argv[i], "--amount")   && i+1 < argc) amount   = atof(argv[++i]);
        if (!strcmp(argv[i], "--max_pct")  && i+1 < argc) max_pct  = atof(argv[++i]);
        if (!strcmp(argv[i], "--max_notional") && i+1 < argc) max_notional = atof(argv[++i]);
        if (!strcmp(argv[i], "--max_size")     && i+1 < argc) max_size     = atoi(argv[++i]);
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
        int signal_present;
        c.signal = extract_double_present(obj, "signal", &signal_present);
        if (!signal_present) {
            fprintf(stderr, "[size_positions/c] %s: missing signal field, skipping (size=0)\n",
                    c.symbol[0] ? c.symbol : "?");
            c.price = 0.0;  /* forces the existing price<=0 skip path below */
        }

        /* FIX CS3: capture the input's meta object verbatim so
         * print_candidate can forward unknown fields (e.g. "strategy")
         * instead of discarding them, same fix as CM3/CR2. */
        {
            const char *meta_p = strstr(obj, "\"meta\"");
            if (meta_p) {
                const char *mb = strchr(meta_p, '{');
                if (mb) {
                    int mdepth = 0; const char *ms = mb;
                    while (*ms) {
                        if (*ms == '{') mdepth++;
                        if (*ms == '}') { mdepth--; if (mdepth == 0) { ms++; break; } }
                        ms++;
                    }
                    size_t mlen = (size_t)(ms - mb);
                    if (mlen >= sizeof(c.meta_raw)) mlen = sizeof(c.meta_raw) - 1;
                    strncpy(c.meta_raw, mb, mlen);
                }
            }
        }
        /* FIX CS5: previously silently defaulted a missing/invalid price
         * to $100.00, producing a wrong share count with no indication
         * anything was off. Leave price <= 0 as-is here; the sizing loop
         * below skips (size=0) any candidate it can't price, matching
         * the fail-closed behavior of the Python size_positions.py fix. */

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

    /* FIX CS6: cap total deployed capital across ALL candidates, not
     * just each individual position's max_pct. Without this, the
     * "fixed" model above deploys n_cands * max_pct of capital in
     * aggregate -- e.g. 10 candidates at 20% each is 200%. Scale the
     * whole vector down proportionally if it would exceed 100%. */
    {
        double total_weight = 0.0;
        for (int i = 0; i < n_cands; i++) total_weight += cands[i].weight;
        if (total_weight > 1.0) {
            fprintf(stderr,
                    "[size_positions/c] total allocation %.1f%% exceeds 100%% "
                    "of capital -- scaling all positions down proportionally\n",
                    total_weight * 100.0);
            for (int i = 0; i < n_cands; i++) cands[i].weight /= total_weight;
        }
    }

    /* ── Set size on each candidate ──────────────────────────── */
    double total_notional = 0.0;
    for (int i = 0; i < n_cands; i++) {
        if (cands[i].price <= 0.0) {
            fprintf(stderr, "[size_positions/c] %s: no usable price, skipping (size=0)\n",
                    cands[i].symbol);
            cands[i].size = 0;
            continue;
        }
        double alloc   = capital * cands[i].weight;
        int    size    = (int)(alloc / cands[i].price);
        if (size < 1) size = 1;

        /* FIX CR5: enforce the limits risk_filter.c could never check. */
        if (size > max_size) {
            fprintf(stderr, "[size_positions/c] %s: size %d > max %d, skipping (size=0)\n",
                    cands[i].symbol, size, max_size);
            cands[i].size = 0;
            continue;
        }
        double notional = size * cands[i].price;
        if (notional > max_notional) {
            fprintf(stderr, "[size_positions/c] %s: notional $%.2f > max $%.2f, skipping (size=0)\n",
                    cands[i].symbol, notional, max_notional);
            cands[i].size = 0;
            continue;
        }

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
