/*
 * Las_shell Pipeline Stage 2 — momentum_filter.c  (C implementation)
 * =================================================================
 * FILTER + ENRICH stage: computes a deterministic momentum signal for
 * each candidate, sets the signal and side fields, and forwards only
 * those whose |signal| meets the threshold.
 *
 * Compile:  gcc -Wall -O2 -o momentum_filter momentum_filter.c -lm
 * Usage:    ./universe | ./momentum_filter
 *           ./universe | ./momentum_filter --threshold 0.3
 *           ./universe | ./momentum_filter --threshold 0.15 --topn 5
 *
 * stdin  : JSON array (convention v1.0)
 * stdout : filtered JSON array with signal + side set
 * stderr : diagnostics only
 *
 * Participates in Las_shell pipelines with zero changes to pipes.c.
 * Fully interchangeable with momentum_filter.py.
 *
 * NOTE: The signal here is a deterministic hash-based proxy for
 * testing purposes. Replace compute_signal() with real momentum
 * math (N-day return, RSI, MACD, etc.) for production use.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MAX_CANDS  64
#define SYM_LEN    16
#define BUFLEN     131072   /* 128 KB — enough for 64 candidates */
#define DEFAULT_THRESHOLD 0.20
#define DEFAULT_TOPN      0    /* 0 = no limit */

/* ── Candidate struct ─────────────────────────────────────────── */
typedef struct {
    char   symbol[SYM_LEN];
    double price;
    double signal;
    char   side[8];    /* "BUY" or "SELL" */
    char   meta_raw[512]; /* raw input meta object content, "" if none */
} Candidate;

/* ── Signal computation ───────────────────────────────────────────
 * Deterministic daily momentum proxy.
 * Replace this function with real momentum logic for production.   */
static double compute_signal(const char *sym) {
    /* Hash symbol + today's date for a stable intraday value */
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    unsigned seed = (unsigned)(lt->tm_year * 10000 + lt->tm_mon * 100
                                + lt->tm_mday);
    for (const char *p = sym; *p; p++)
        seed = seed * 31 + (unsigned char)*p;

    srand(seed);
    /* Simulate 20-day return as sum of daily Gaussian returns */
    double cum = 1.0;
    for (int i = 0; i < 20; i++) {
        double r = ((double)rand() / RAND_MAX - 0.5) * 0.030; /* ±1.5% daily */
        cum *= (1.0 + r);
    }
    double raw = cum - 1.0;
    /* Clip to [-0.15, 0.15] and normalise to [-1, 1] */
    if (raw >  0.15) raw =  0.15;
    if (raw < -0.15) raw = -0.15;
    return round(raw / 0.15 * 1000.0) / 1000.0;
}

/* ── Tiny JSON helpers ────────────────────────────────────────── */
/* Extract value of first "key":"value" or "key":number after *p.
 * Returns pointer past the extracted token, or NULL on failure.   */
static const char *extract_string(const char *p, const char *key,
                                   char *out, int outlen) {
    char search[32];
    snprintf(search, sizeof(search), "\"%s\"", key);
    p = strstr(p, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return NULL;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < outlen - 1) out[i++] = *p++;
    out[i] = '\0';
    return *p ? p + 1 : NULL;
}

static double extract_double(const char *buf, const char *key) {
    char search[32];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(buf, search);
    if (!p) return 0.0;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    return atof(p);
}

/* FIX CM3: re-emit any "key": value pairs from a raw meta object text
 * that aren't one of this stage's own reserved/overridden keys, so
 * upstream fields (e.g. "strategy") set by earlier stages survive
 * instead of being silently discarded. Simple scanner, not a full JSON
 * parser -- handles the flat string/number meta fields this pipeline
 * actually uses (see PIPELINE_CONVENTION.md). */
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

/* ── Print one candidate as JSON ─────────────────────────────── */
static void print_candidate(const Candidate *c, int first) {
    char ts[32];
    time_t now = time(NULL);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    if (!first) printf(",\n");
    printf("  {\n");
    printf("    \"symbol\": \"%s\",\n",  c->symbol);
    printf("    \"signal\": %.4f,\n",     c->signal);
    printf("    \"size\":   0,\n");
    printf("    \"price\":  %.2f,\n",     c->price);
    printf("    \"side\":   \"%s\",\n",   c->side);
    printf("    \"meta\": {\n");
    printf("      \"_convention\":  \"1.0\",\n");
    printf("      \"stage\":        \"momentum_filter\",\n");
    printf("      \"language\":     \"c\",\n");
    printf("      \"timestamp\":    \"%s\"", ts);
    static const char *reserved[] = {"_convention", "stage", "language", "timestamp"};
    print_passthrough_meta_fields(c->meta_raw, reserved, 4);
    printf("\n    }\n");
    printf("  }");
}

/* ── qsort comparator: descending |signal| ────────────────────── */
static int cmp_signal_desc(const void *a, const void *b) {
    double sa = fabs(((const Candidate *)a)->signal);
    double sb = fabs(((const Candidate *)b)->signal);
    if (sb > sa) return  1;
    if (sb < sa) return -1;
    return 0;
}

/* ── main ─────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    double threshold = DEFAULT_THRESHOLD;
    int    topn      = DEFAULT_TOPN;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--threshold") == 0 && i + 1 < argc)
            threshold = atof(argv[++i]);
        else if (strcmp(argv[i], "--topn") == 0 && i + 1 < argc)
            topn = atoi(argv[++i]);
    }

    /* ── Read stdin ─────────────────────────────────────────── */
    char *buf = malloc(BUFLEN);
    if (!buf) { fprintf(stderr, "[momentum_filter/c] malloc failed\n"); return 1; }
    size_t n = fread(buf, 1, BUFLEN - 1, stdin);
    buf[n] = '\0';

    if (n == 0 || strcmp(buf, "[]") == 0) {
        printf("[]\n");
        fprintf(stderr, "[momentum_filter/c] empty input\n");
        free(buf); return 0;
    }

    /* ── Parse candidates ────────────────────────────────────── */
    Candidate cands[MAX_CANDS];
    int n_in = 0;
    const char *p = buf;

    while ((p = strstr(p, "\"symbol\"")) != NULL && n_in < MAX_CANDS) {
        Candidate c = {0};
        /* Extract symbol */
        p += 8;
        while (*p && *p != '"') p++;
        if (!*p) break;
        p++;
        int i = 0;
        while (*p && *p != '"' && i < SYM_LEN - 1) c.symbol[i++] = *p++;
        c.symbol[i] = '\0';

        /* Extract price from the same object.
         * FIX (found while fixing CM3): find the TRUE end of this
         * candidate object via depth-tracking, not the first '}' --
         * that could be the nested meta object's own closing brace,
         * silently truncating anything (like meta) that comes after it. */
        const char *obj_start = p;
        int depth = 0;
        const char *scan = p;
        while (*scan) {
            if (*scan == '{') depth++;
            if (*scan == '}') { if (depth == 0) break; depth--; }
            scan++;
        }
        const char *obj_end = scan;
        if (!*obj_end) break;
        char obj_buf[1024] = {0};
        size_t obj_len = (size_t)(obj_end - obj_start);
        if (obj_len >= sizeof(obj_buf)) obj_len = sizeof(obj_buf) - 1;
        strncpy(obj_buf, obj_start, obj_len);
        c.price = extract_double(obj_buf, "price");
        if (c.price <= 0.0) c.price = 100.0;  /* fallback */

        /* FIX CM3: capture the input's meta object verbatim (instead of
         * discarding it) so print_candidate can forward unknown fields
         * (e.g. "strategy") set by earlier stages, only overriding the
         * fields this stage is actually responsible for. */
        const char *meta_p = strstr(obj_buf, "\"meta\"");
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

        /* Compute signal */
        c.signal = compute_signal(c.symbol);
        strcpy(c.side, c.signal > 0 ? "BUY" : "SELL");

        cands[n_in++] = c;
    }

    /* ── Filter by threshold ─────────────────────────────────── */
    Candidate passed[MAX_CANDS];
    int n_pass = 0;

    for (int i = 0; i < n_in; i++) {
        if (fabs(cands[i].signal) >= threshold) {
            passed[n_pass++] = cands[i];
        } else {
            fprintf(stderr, "[momentum_filter/c] FILTERED %s "
                    "(signal=%.3f < %.3f)\n",
                    cands[i].symbol, cands[i].signal, threshold);
        }
    }

    /* ── Rank by |signal| descending, optionally trim to topN ── */
    qsort(passed, n_pass, sizeof(Candidate), cmp_signal_desc);
    if (topn > 0 && topn < n_pass) n_pass = topn;

    /* ── Emit JSON array ─────────────────────────────────────── */
    printf("[\n");
    for (int i = 0; i < n_pass; i++)
        print_candidate(&passed[i], i == 0);
    printf("\n]\n");

    fprintf(stderr,
            "[momentum_filter/c] %d in -> %d passed "
            "(threshold=%.2f, topn=%s%d)\n",
            n_in, n_pass, threshold,
            topn > 0 ? "top" : "all/", topn > 0 ? topn : n_pass);

    free(buf);
    return 0;
}
