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

/* ── JSON helpers ─────────────────────────────────────────────── */
static void print_candidate(const char* sym, double price,
                             int first, int last) {
    (void)last;
    char ts[32];
    time_t now = time(NULL);
    struct tm* lt = localtime(&now);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", lt);

    if (!first) printf(",\n");
    printf("  {\n");
    printf("    \"symbol\": \"%s\",\n", sym);
    printf("    \"signal\": 0.0,\n");
    printf("    \"size\":   0,\n");
    printf("    \"price\":  %.2f,\n", price);
    printf("    \"side\":   \"BUY\",\n");
    printf("    \"meta\": {\n");
    printf("      \"_convention\": \"1.0\",\n");
    printf("      \"strategy\":    \"las_shell_pipeline\",\n");
    printf("      \"stage\":       \"universe\",\n");
    printf("      \"language\":    \"c\",\n");
    printf("      \"timestamp\":   \"%s\"\n", ts);
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

    /* Output JSON array */
    printf("[\n");
    for (int i = 0; i < n; i++) {
        /* Find price — use default if available */
        double price = 100.0;
        for (int j = 0; DEFAULT_SYMBOLS[j]; j++) {
            if (strcmp(symbols[i], DEFAULT_SYMBOLS[j]) == 0) {
                price = sim_price(symbols[i], DEFAULT_PRICES[j]);
                break;
            }
        }
        print_candidate(symbols[i], price, (i == 0), (i == n-1));
    }
    printf("\n]\n");

    fprintf(stderr, "[universe/c] generated %d candidates\n", n);
    return 0;
}