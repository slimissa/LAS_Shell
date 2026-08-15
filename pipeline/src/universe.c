/*
 * Las_shell Pipeline Stage 1 — universe.c  (C implementation)
 * =========================================================
 * SOURCE stage: generates a candidate list from a hardcoded or
 * environment-defined watchlist. Outputs JSON array to stdout.
 *
 * Compile:  gcc -Wall -O2 -o universe universe.c -lm
 * Usage:    ./universe
 *           ./universe --top 5
 *           WATCHLIST="AAPL,MSFT,NVDA" ./universe
 *
 * Participates in Las_shell pipelines with zero changes to pipes.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>

#define MAX_SYMBOLS 64
#define SYM_LEN     16

/* ── Simulated prices ─────────────────────────────────────────── */
static const char* DEFAULT_SYMBOLS[] = {
    "AAPL","MSFT","GOOGL","AMZN","NVDA","META","TSLA","SPY","QQQ","GLD", NULL
};
static const double DEFAULT_PRICES[] = {
    185.0, 415.0, 175.0, 195.0, 875.0, 510.0, 175.0, 510.0, 435.0, 195.0
};

static double sim_price(const char* sym, double base) {
    /* Deterministic daily price using date + symbol as seed */
    time_t now = time(NULL);
    struct tm* lt = localtime(&now);
    unsigned seed = (unsigned)(lt->tm_year * 10000 + lt->tm_mon * 100 + lt->tm_mday);
    for (const char* p = sym; *p; p++) seed = seed * 31 + (unsigned char)*p;
    srand(seed);
    double noise = ((double)rand() / RAND_MAX - 0.5) * 0.016;
    return round((base * (1.0 + noise)) * 100.0) / 100.0;
}

/* ── Milestone 1 (v0.6.0): live feed integration ──────────────────
 * Shared read point with pipeline/universe.py: both read the same
 * cache file that scripts/live_price_feed.py writes, so the C and
 * Python pipeline paths consume byte-identical real data instead of
 * diverging synthetic RNGs (the whole point of this milestone — see
 * roadmap "C/Python RNG divergence becomes moot"). universe.c does not
 * reimplement HTTP/CSV parsing; it shells out to the one Python fetcher
 * to keep that logic in a single place, then reads the resulting JSON
 * cache with the same minimal-scanner idiom pipeline/src/risk_filter.c
 * already uses for its JSON fields. */

typedef struct {
    double price;
    char   source[24];       /* "stooq" | "synthetic_fallback" | "" */
    char   fetch_status[40];
    int    found;
} LivePrice;

/* Minimal JSON field extractors — same pattern as risk_filter.c's
 * extract_str/extract_num, scoped to a caller-supplied substring so we
 * can search within one ticker's object without matching a sibling
 * ticker's fields of the same name. */
static int lf_extract_str(const char* json, const char* key,
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

static int lf_extract_num(const char* json, const char* key, double* out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    *out = atof(p);
    return 1;
}

/* Finds "SYMBOL": { ... } within the cache's top-level object and
 * returns a heap copy of just that ticker's { ... } block (matching
 * braces, so nested objects don't confuse the scan). Caller frees. */
static char* lf_find_ticker_block(const char* cache_json, const char* symbol) {
    char key[SYM_LEN + 4];
    snprintf(key, sizeof(key), "\"%.*s\"", SYM_LEN - 1, symbol);
    const char* p = strstr(cache_json, key);
    if (!p) return NULL;
    p = strchr(p + strlen(key), '{');
    if (!p) return NULL;
    int depth = 0;
    const char* start = p;
    const char* q = p;
    while (*q) {
        if (*q == '{') depth++;
        else if (*q == '}') {
            depth--;
            if (depth == 0) { q++; break; }
        }
        q++;
    }
    if (depth != 0) return NULL;
    size_t len = (size_t)(q - start);
    char* block = malloc(len + 1);
    if (!block) return NULL;
    memcpy(block, start, len);
    block[len] = '\0';
    return block;
}

static char* slurp_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char* buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

/* Invokes the shared Python fetcher for every symbol in one call (one
 * process spawn, not one per symbol) so the cache is refreshed, then
 * reads that same cache file for every ticker's entry. Returns 1 per
 * symbol found via `out[i]`; symbols the cache doesn't have (fetcher
 * missing, python3 missing, total feed outage) are left with found=0
 * so the caller falls back to sim_price for exactly those symbols —
 * not all-or-nothing. */
static void fetch_live_prices(char symbols[][SYM_LEN], int n, LivePrice* out) {
    for (int i = 0; i < n; i++) out[i].found = 0;

    const char* lhome = getenv("LAS_SHELL_HOME");
    if (!lhome || !*lhome) lhome = ".";

    char cache_path[1024];
    snprintf(cache_path, sizeof(cache_path), "%s/logs/live_price_cache.json", lhome);

    /* Build: python3 $LAS_SHELL_HOME/scripts/live_price_feed.py SYM1 SYM2 ... --cache PATH */
    char cmd[4096];
    int off = snprintf(cmd, sizeof(cmd),
                        "python3 '%s/scripts/live_price_feed.py'", lhome);
    for (int i = 0; i < n && off < (int)sizeof(cmd) - 32; i++) {
        off += snprintf(cmd + off, sizeof(cmd) - off, " '%s'", symbols[i]);
    }
    off += snprintf(cmd + off, sizeof(cmd) - off,
                     " --cache '%s' >/dev/null 2>&1", cache_path);

    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "[universe/c] live feed invocation failed (rc=%d) — "
                         "falling back to synthetic for all symbols\n", rc);
        return; /* every LivePrice stays found=0 -> sim_price fallback */
    }

    char* cache_json = slurp_file(cache_path);
    if (!cache_json) {
        fprintf(stderr, "[universe/c] could not read live price cache at %s — "
                         "falling back to synthetic for all symbols\n", cache_path);
        return;
    }

    for (int i = 0; i < n; i++) {
        char* block = lf_find_ticker_block(cache_json, symbols[i]);
        if (!block) continue; /* leave found=0, sim_price fallback for this one */
        double price;
        if (lf_extract_num(block, "price", &price)) {
            out[i].price = price;
            lf_extract_str(block, "source", out[i].source, sizeof(out[i].source));
            lf_extract_str(block, "fetch_status", out[i].fetch_status, sizeof(out[i].fetch_status));
            out[i].found = 1;
        }
        free(block);
    }
    free(cache_json);
}

/* ── JSON helpers ─────────────────────────────────────────────── */

/* FIX CU6: escape a string for safe embedding inside a JSON string
 * literal. sym comes from WATCHLIST/argv (user/environment-influenced);
 * printing it unescaped let a value containing '"' or '\' break out of
 * the JSON string and inject arbitrary content into the candidate
 * stream this feeds downstream. */
static void print_json_escaped(const char* s) {
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", stdout); break;
            case '\\': fputs("\\\\", stdout); break;
            case '\n': fputs("\\n",  stdout); break;
            case '\r': fputs("\\r",  stdout); break;
            case '\t': fputs("\\t",  stdout); break;
            default:
                if (*p < 0x20) printf("\\u%04x", *p);
                else putchar(*p);
        }
    }
}

static void print_candidate(const char* sym, double price,
                             const char* data_source, const char* fetch_status,
                             int first, int last) {
    (void)last;
    char ts[32];
    time_t now = time(NULL);
    struct tm* lt = localtime(&now);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", lt);

    if (!first) printf(",\n");
    printf("  {\n");
    printf("    \"symbol\": \"");
    print_json_escaped(sym);
    printf("\",\n");
    printf("    \"signal\": 0.0,\n");
    printf("    \"size\":   0,\n");
    printf("    \"price\":  %.2f,\n", price);
    printf("    \"side\":   \"BUY\",\n");
    printf("    \"meta\": {\n");
    printf("      \"_convention\": \"1.0\",\n");
    printf("      \"strategy\":    \"las_shell_pipeline\",\n");
    printf("      \"stage\":       \"universe\",\n");
    printf("      \"language\":    \"c\",\n");
    printf("      \"timestamp\":   \"%s\",\n", ts);
    printf("      \"data_source\": \"");
    print_json_escaped(data_source);
    printf("\",\n");
    printf("      \"fetch_status\": \"");
    print_json_escaped(fetch_status);
    printf("\"\n");
    printf("    }\n");
    printf("  }");
}

int main(int argc, char** argv) {
    char  symbols[MAX_SYMBOLS][SYM_LEN];
    int   n = 0;
    int   top_n = 0;

    /* Parse args */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--top") == 0 && i+1 < argc) {
            top_n = atoi(argv[++i]);
        }
    }

    /* Check WATCHLIST env var */
    char* wl = getenv("WATCHLIST");
    if (wl) {
        char buf[1024];
        strncpy(buf, wl, sizeof(buf)-1);
        char* tok = strtok(buf, ",");
        while (tok && n < MAX_SYMBOLS) {
            strncpy(symbols[n], tok, SYM_LEN-1);
            symbols[n][SYM_LEN-1] = '\0';
            n++;
            tok = strtok(NULL, ",");
        }
    } else {
        for (int i = 0; DEFAULT_SYMBOLS[i] && n < MAX_SYMBOLS; i++) {
            strncpy(symbols[n], DEFAULT_SYMBOLS[i], SYM_LEN-1);
            n++;
        }
    }

    if (top_n > 0 && top_n < n) n = top_n;

    /* Milestone 1 (v0.6.0): live feed is the default; LAS_SHELL_LIVE_FEED=0
     * forces the pure-synthetic path (documented fallback). */
    const char* live_env = getenv("LAS_SHELL_LIVE_FEED");
    int use_live = !(live_env && strcmp(live_env, "0") == 0);

    LivePrice* live = NULL;
    if (use_live) {
        live = calloc((size_t)n, sizeof(LivePrice));
        if (live) fetch_live_prices(symbols, n, live);
    }

    int n_real = 0, n_fallback = 0;

    /* Output JSON array */
    printf("[\n");
    for (int i = 0; i < n; i++) {
        /* Find base price for synthetic fallback */
        double base = 100.0;
        for (int j = 0; DEFAULT_SYMBOLS[j]; j++) {
            if (strcmp(symbols[i], DEFAULT_SYMBOLS[j]) == 0) {
                base = DEFAULT_PRICES[j];
                break;
            }
        }

        double price;
        const char* data_source;
        const char* fetch_status;

        if (live && live[i].found) {
            price = live[i].price;
            data_source = live[i].source[0] ? live[i].source : "stooq";
            fetch_status = live[i].fetch_status[0] ? live[i].fetch_status : "ok";
            if (strcmp(data_source, "stooq") == 0) n_real++; else n_fallback++;
        } else {
            price = sim_price(symbols[i], base);
            data_source = use_live ? "synthetic_fallback" : "synthetic";
            fetch_status = use_live ? "n/a:no_cache_entry" : "n/a";
            n_fallback++;
        }

        print_candidate(symbols[i], price, data_source, fetch_status,
                         (i == 0), (i == n-1));
    }
    printf("\n]\n");

    if (use_live) {
        fprintf(stderr, "[universe/c] live feed: %d real, %d synthetic_fallback\n",
                n_real, n_fallback);
    }
    free(live);

    fprintf(stderr, "[universe/c] generated %d candidates\n", n);
    return 0;
}