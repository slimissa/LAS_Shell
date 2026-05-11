/*
 * Las_shell Pipeline Stage 3 — risk_filter.c  (C implementation)
 * =============================================================
 * FILTER stage: reads JSON candidates from stdin, applies risk rules,
 * writes passing candidates to stdout. Works as a ?> gate (exit 1
 * on any rejection).
 *
 * This implementation uses a minimal hand-rolled JSON scanner — no
 * external library needed. For production, replace with cJSON or jansson.
 *
 * Compile:  gcc -Wall -O2 -o risk_filter risk_filter.c
 * Usage:    ./universe | python3 momentum_filter.py | ./risk_filter
 *           echo '[{"symbol":"GME",...}]' ?> ./risk_filter --gate
 *           ./risk_filter --max_notional 200000 --max_size 1000
 *
 * Participates in Las_shell pipelines with zero changes to pipes.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MAX_INPUT   (1 << 20)   /* 1 MB max input */
#define MAX_CANDS   256
#define SYM_LEN     16
#define REASON_LEN  128

/* ── Risk limits ──────────────────────────────────────────────── */
static double g_max_notional = 500000.0;
static int    g_max_size     = 5000;
static double g_min_signal   = 0.1;
static int    g_gate_mode    = 0;

static const char* BLACKLIST[] = {
    "GME","AMC","BBBY","MULN","FFIE","SPCE", NULL
};

/* ── Candidate struct ─────────────────────────────────────────── */
typedef struct {
    char   symbol[SYM_LEN];
    double signal;
    int    size;
    double price;
    char   side[8];
    int    passed;
    char   reason[REASON_LEN];
} Candidate;

/* ── Minimal JSON field extractor ─────────────────────────────── */
static int extract_str(const char* json, const char* key,
                        char* out, int outlen) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return 0;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < outlen-1) out[i++] = *p++;
    out[i] = '\0';
    return 1;
}

static double extract_num(const char* json, const char* key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return 0.0;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    return atof(p);
}

/* ── Risk check ───────────────────────────────────────────────── */
static int check(Candidate* c) {
    /* Blacklist */
    for (int i = 0; BLACKLIST[i]; i++) {
        if (strcmp(c->symbol, BLACKLIST[i]) == 0) {
            snprintf(c->reason, REASON_LEN, "%s is blacklisted", c->symbol);
            return 0;
        }
    }
    /* Signal floor */
    if (fabs(c->signal) < g_min_signal) {
        snprintf(c->reason, REASON_LEN, "signal %.3f < min %.3f",
                 c->signal, g_min_signal);
        return 0;
    }
    /* Size / notional */
    if (c->size > 0) {
        if (c->size > g_max_size) {
            snprintf(c->reason, REASON_LEN, "size %d > max %d",
                     c->size, g_max_size);
            return 0;
        }
        double notional = c->size * c->price;
        if (notional > g_max_notional) {
            snprintf(c->reason, REASON_LEN,
                     "notional $%.0f > max $%.0f", notional, g_max_notional);
            return 0;
        }
    }
    snprintf(c->reason, REASON_LEN, "ok");
    return 1;
}

/* ── Log rejection ────────────────────────────────────────────── */
static void log_rejection(const Candidate* c) {
    char* home = getenv("HOME");
    if (!home) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/.las_shell_risk_rejections", home);
    FILE* f = fopen(path, "a");
    if (!f) return;
    time_t now = time(NULL);
    struct tm* lt = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", lt);
    fprintf(f, "%s,REJECTED,%s,%s\n", ts, c->symbol, c->reason);
    fclose(f);
}

/* ── Split JSON array into objects ────────────────────────────── */
static int split_objects(char* buf, char* objects[], int max_obj) {
    int depth = 0, n = 0;
    char* start = NULL;
    for (char* p = buf; *p; p++) {
        if (*p == '{') { if (depth++ == 0) start = p; }
        else if (*p == '}') {
            if (--depth == 0 && start && n < max_obj) {
                *(p+1) = '\0';
                objects[n++] = start;
                start = NULL;
            }
        }
    }
    return n;
}

int main(int argc, char** argv) {
    /* Parse args */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--max_notional") == 0 && i+1 < argc)
            g_max_notional = atof(argv[++i]);
        else if (strcmp(argv[i], "--max_size") == 0 && i+1 < argc)
            g_max_size = atoi(argv[++i]);
        else if (strcmp(argv[i], "--min_signal") == 0 && i+1 < argc)
            g_min_signal = atof(argv[++i]);
        else if (strcmp(argv[i], "--gate") == 0)
            g_gate_mode = 1;
    }

    /* Read stdin */
    char* buf = malloc(MAX_INPUT);
    if (!buf) { perror("malloc"); return 1; }
    size_t total = 0;
    int c;
    while ((c = getchar()) != EOF && total < MAX_INPUT-1)
        buf[total++] = (char)c;
    buf[total] = '\0';

    if (total == 0 || strcmp(buf, "[]") == 0) {
        printf("[]\n"); free(buf); return 0;
    }

    /* Split into objects */
    char* objs[MAX_CANDS];
    int n = split_objects(buf, objs, MAX_CANDS);

    /* Parse and check each candidate */
    Candidate cands[MAX_CANDS];
    int passed_count = 0, rejected_count = 0;

    for (int i = 0; i < n; i++) {
        extract_str(objs[i], "symbol", cands[i].symbol, SYM_LEN);
        extract_str(objs[i], "side",   cands[i].side,   8);
        cands[i].signal = extract_num(objs[i], "signal");
        cands[i].size   = (int)extract_num(objs[i], "size");
        cands[i].price  = extract_num(objs[i], "price");
        cands[i].passed = check(&cands[i]);

        if (cands[i].passed) passed_count++;
        else {
            rejected_count++;
            fprintf(stderr, "[risk_filter/c] REJECTED %s: %s\n",
                    cands[i].symbol, cands[i].reason);
            log_rejection(&cands[i]);
        }
    }

    /* Output passing candidates */
    printf("[\n");
    int first = 1;
    for (int i = 0; i < n; i++) {
        if (!cands[i].passed) continue;
        if (!first) printf(",\n");
        first = 0;
        printf("  {\n");
        printf("    \"symbol\": \"%s\",\n", cands[i].symbol);
        printf("    \"signal\": %.4f,\n",   cands[i].signal);
        printf("    \"size\":   %d,\n",      cands[i].size);
        printf("    \"price\":  %.2f,\n",    cands[i].price);
        printf("    \"side\":   \"%s\",\n",  cands[i].side);
        printf("    \"meta\": {\"stage\": \"risk_filter\", \"language\": \"c\", \"risk_status\": \"PASSED\"}\n");
        printf("  }");
    }
    printf("\n]\n");

    fprintf(stderr,
            "[risk_filter/c] %d in → %d passed, %d rejected\n",
            n, passed_count, rejected_count);

    free(buf);

    /* Gate mode: exit 1 if any rejection */
    if (g_gate_mode && rejected_count > 0) return 1;
    return 0;
}