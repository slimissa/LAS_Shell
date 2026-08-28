/* ============================================================
 * Las_shell v0.6.0 Milestone 3 — ISO 4217 Currency Library
 * src/currency.c
 *
 * No external JSON dependency (roadmap requirement) -- uses a
 * minimal scoped-substring field extractor, same idiom as
 * pipeline/src/risk_filter.c's extract_str/extract_num and
 * src/broker.c's json_field(): find a "key", skip to the value,
 * copy until the closing quote/comma. Not a general JSON parser --
 * it only needs to walk one known, machine-generated file shape.
 *
 * Design decisions:
 *   - Only currencies.active[] is loaded (roadmap: "Only active
 *     ISO 4217 currencies in the lookup table"). withdrawn[] and
 *     the non_iso.{cryptocurrencies,stablecoins,commodities,
 *     special_purpose} groups are intentionally not indexed.
 *   - Each currency object's "countries" array is truncated out of
 *     the substring before field extraction, because "name" (and
 *     potentially other field names) can recur inside each country
 *     entry -- truncating avoids ever matching a nested field
 *     instead of the top-level one, rather than relying on field
 *     order always putting scalars before "countries" (which is
 *     true today, but a strstr-first-match approach without the
 *     truncation would be silently fragile against future registry
 *     versions that reorder fields).
 *   - Binary search on a code-sorted array (roadmap: "Binary search
 *     or hash table for O(log n) or O(1) lookup"); 167 entries makes
 *     either approach fast, binary search needs no hashing code.
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L

#include "../include/currency.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>

#define CURRENCY_MAX_ENTRIES 512  /* registry has 167 active; generous headroom */

static Currency g_table[CURRENCY_MAX_ENTRIES];
static int      g_count = 0;
static int      g_loaded = 0;

/* ============================================================
 * Minimal JSON field extraction (scoped to one object substring)
 * ============================================================ */

static char* slurp_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

/* extract_str: find "key" in json, copy the following string value
 * (raw bytes between the quotes, handling \" and \\ escapes) into
 * out. Returns 1 on success, 0 if key not found or value isn't a
 * string (e.g. null). */
static int extract_str(const char *json, const char *key, char *out, int outlen) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ':') p++;
    if (*p != '"') return 0; /* null or non-string value */
    p++;
    int i = 0;
    while (*p && *p != '"' && i < outlen - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++; /* copy the escaped char verbatim (\" -> ", \\ -> \) */
            out[i++] = *p++;
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = '\0';
    return 1;
}

static int extract_int(const char *json, const char *key, int *out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ':') p++;
    char *endp = NULL;
    long v = strtol(p, &endp, 10);
    if (endp == p) return 0; /* not a number (e.g. null) */
    *out = (int)v;
    return 1;
}

/* Truncate a currency object's substring right before its
 * "countries" key, in place, so field extraction can never wander
 * into a nested per-country object. Safe no-op if "countries" is
 * absent. */
static void truncate_before_countries(char *obj) {
    char *p = strstr(obj, "\"countries\"");
    if (p) *p = '\0';
}

/* Find the substring for the Nth top-level `{...}` object inside a
 * JSON array starting at `arr_start` (which must point at the '[').
 * Depth-tracks both {} and [] so nested arrays/objects don't confuse
 * the scan. Returns a malloc'd copy of the object (caller frees), or
 * NULL when there are no more objects before the array's closing ']'
 * at depth 0. *cursor is advanced past the object found so repeated
 * calls walk the whole array. */
static char* next_array_object(const char **cursor) {
    const char *p = *cursor;
    /* skip to the next '{' or the array's closing ']' */
    while (*p && *p != '{' && *p != ']') p++;
    if (*p != '{') { *cursor = p; return NULL; }

    const char *start = p;
    int depth = 0;
    while (*p) {
        if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') {
            depth--;
            if (depth == 0) { p++; break; }
        } else if (*p == '"') {
            /* skip over string contents so a '{' or '}' inside a
             * string value can never desync the depth counter */
            p++;
            while (*p && *p != '"') { if (*p == '\\' && *(p + 1)) p++; p++; }
        }
        p++;
    }
    size_t len = (size_t)(p - start);
    char *obj = malloc(len + 1);
    if (!obj) return NULL;
    memcpy(obj, start, len);
    obj[len] = '\0';
    *cursor = p;
    return obj;
}

/* Find `"currencies": { ... "active": [ ... ] ... }` and return a
 * pointer to the '[' that starts the active array, or NULL. */
static const char* find_active_array(const char *json) {
    const char *cur = strstr(json, "\"currencies\"");
    if (!cur) return NULL;
    const char *act = strstr(cur, "\"active\"");
    if (!act) return NULL;
    const char *br = strchr(act, '[');
    return br;
}

static int cmp_currency_code(const void *a, const void *b) {
    return strcmp(((const Currency *)a)->code, ((const Currency *)b)->code);
}

/* ============================================================
 * Loading
 * ============================================================ */

static void install_builtin_fallback(void) {
    g_count = 0;
    Currency *c = &g_table[g_count++];
    memset(c, 0, sizeof(*c));
    strcpy(c->code, "USD");
    strcpy(c->numeric, "840");
    strcpy(c->name, "US Dollar");
    c->minor_units = 2;
    strcpy(c->symbol, "$");
    c->is_active = 1;
    fprintf(stderr,
        "[currency] WARNING: iso4217.json not found or unreadable -- "
        "using built-in USD-only fallback. Set LAS_SHELL_HOME or pass "
        "an explicit path to currency_init() to load the full registry.\n");
}

static int load_from_file(const char *path) {
    char *json = slurp_file(path);
    if (!json) return -1;

    const char *arr = find_active_array(json);
    if (!arr) {
        fprintf(stderr,
            "[currency] ERROR: %s does not contain currencies.active[] "
            "-- registry file may be malformed or from an incompatible "
            "schema version.\n", path);
        free(json);
        return -1;
    }

    const char *cursor = arr + 1; /* past the '[' */
    g_count = 0;
    char *obj;
    while (g_count < CURRENCY_MAX_ENTRIES && (obj = next_array_object(&cursor)) != NULL) {
        truncate_before_countries(obj);

        Currency c;
        memset(&c, 0, sizeof(c));
        int have_code = extract_str(obj, "code", c.code, sizeof(c.code));
        extract_str(obj, "numeric", c.numeric, sizeof(c.numeric));
        extract_str(obj, "name", c.name, sizeof(c.name));
        int mu = 2;
        extract_int(obj, "minor_units", &mu);
        c.minor_units = mu;
        extract_str(obj, "symbol", c.symbol, sizeof(c.symbol));
        c.is_active = 1;

        free(obj);

        if (!have_code || !c.code[0]) continue; /* malformed entry, skip */
        for (char *u = c.code; *u; u++) *u = (char)toupper((unsigned char)*u);
        g_table[g_count++] = c;
    }
    free(json);

    if (g_count == 0) {
        fprintf(stderr,
            "[currency] ERROR: %s parsed but yielded zero active currencies.\n",
            path);
        return -1;
    }

    qsort(g_table, (size_t)g_count, sizeof(Currency), cmp_currency_code);
    return 0;
}

int currency_init(const char *path) {
    /* 1. explicit path */
    if (path && path[0] && access(path, R_OK) == 0) {
        if (load_from_file(path) == 0) { g_loaded = 1; return 0; }
    }

    /* 2. $LAS_SHELL_HOME/iso4217.json */
    const char *home = getenv("LAS_SHELL_HOME");
    if (home && home[0]) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s/iso4217.json", home);
        if (access(buf, R_OK) == 0 && load_from_file(buf) == 0) {
            g_loaded = 1;
            return 0;
        }
    }

    /* 3. default install path */
    if (access("/usr/local/share/las_shell/iso4217.json", R_OK) == 0 &&
        load_from_file("/usr/local/share/las_shell/iso4217.json") == 0) {
        g_loaded = 1;
        return 0;
    }

    /* 4. built-in fallback -- this is a successful (degraded) init,
     * not a failure, so currency_lookup("USD") always works even
     * with zero configuration. */
    install_builtin_fallback();
    g_loaded = 1;
    return 0;
}

void currency_cleanup(void) {
    g_count = 0;
    g_loaded = 0;
}

/* ============================================================
 * Lookup
 * ============================================================ */

const Currency* currency_lookup(const char *code) {
    if (!g_loaded || !code || !code[0]) return NULL;

    char key[8];
    size_t n = strlen(code);
    if (n == 0 || n >= sizeof(key)) return NULL;
    for (size_t i = 0; i < n; i++) key[i] = (char)toupper((unsigned char)code[i]);
    key[n] = '\0';

    int lo = 0, hi = g_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int c = strcmp(g_table[mid].code, key);
        if (c == 0) return &g_table[mid];
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

int currency_is_valid(const char *code) {
    return currency_lookup(code) != NULL;
}

/* ============================================================
 * Conversion
 * ============================================================ */

/* Round half away from zero -- matches the registry's stated
 * cross-language behavior (2.5 -> 3, -2.5 -> -3), not banker's
 * rounding. */
static double round_half_away_from_zero(double x) {
    return (x >= 0.0) ? floor(x + 0.5) : ceil(x - 0.5);
}

long long currency_to_minor(const char *code, double amount) {
    int minor_units = 2; /* defensive default for unrecognized codes */
    const Currency *c = currency_lookup(code);
    if (c) minor_units = c->minor_units;

    double scale = pow(10.0, minor_units);
    return (long long)round_half_away_from_zero(amount * scale);
}

double currency_from_minor(const char *code, long long minor) {
    int minor_units = 2;
    const Currency *c = currency_lookup(code);
    if (c) minor_units = c->minor_units;

    double scale = pow(10.0, minor_units);
    return (double)minor / scale;
}

/* ============================================================
 * Display
 * ============================================================ */

void currency_list_all(void) {
    if (!g_loaded) {
        fprintf(stderr, "[currency] not initialized\n");
        return;
    }
    fprintf(stderr, "%-4s  %-6s  %-3s  %-40s  %s\n",
            "CODE", "SYMBOL", "MU", "NAME", "NUMERIC");
    for (int i = 0; i < g_count; i++) {
        const Currency *c = &g_table[i];
        fprintf(stderr, "%-4s  %-6s  %-3d  %-40s  %s\n",
                c->code, c->symbol, c->minor_units, c->name, c->numeric);
    }
    fprintf(stderr, "(%d active currencies)\n", g_count);
}
