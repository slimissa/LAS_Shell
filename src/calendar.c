/* ============================================================
 * Las_shell v0.6.0 Milestone 3 — Exchange Calendar Library
 * src/calendar.c
 *
 * See include/calendar.h for the public API contract and the
 * timezone-handling tradeoff (TZ-env-var + mutex, not reentrant
 * zoneinfo functions -- glibc on this platform doesn't expose
 * tzalloc/localtime_rz).
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "../include/calendar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>

#define CAL_MAX_EXCHANGES   64
#define CAL_MAX_HOLIDAYS    128   /* per exchange; registry has ~60-90 over 5yr */
#define CAL_TZ_LEN          64
#define CAL_NAME_LEN        128

typedef struct {
    char date[11];              /* "YYYY-MM-DD" */
    int  is_closed;             /* status == "closed" */
    int  early_close_min;       /* minutes since midnight, -1 if not early-close */
} HolidayEntry;

typedef struct {
    char code[8];                /* MIC, e.g. "XNYS" */
    char name[CAL_NAME_LEN];
    char timezone[CAL_TZ_LEN];   /* IANA zone name */

    int  regular_open_min;       /* minutes since midnight, local */
    int  regular_close_min;
    int  has_pre;
    int  pre_open_min, pre_close_min;
    int  has_after;
    int  after_open_min, after_close_min;
    int  has_lunch;
    int  lunch_open_min, lunch_close_min;

    HolidayEntry holidays[CAL_MAX_HOLIDAYS];
    int  holiday_count;

    char gen_range_start[11];
    char gen_range_end[11];
} Exchange;

static Exchange g_exchanges[CAL_MAX_EXCHANGES];
static int      g_exchange_count = 0;
static int      g_loaded = 0;

/* Serializes every setenv(TZ)/tzset()/localtime_r()-or-mktime()
 * sequence against concurrent use from other threads (notably the
 * checkpoint thread, which also formats local timestamps). See
 * calendar.h for the tradeoff this implies. */
static pthread_mutex_t g_tz_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================
 * Small alias table (Phase 6 requirement, resolved here so every
 * calendar_*() entry point benefits, not just the operators)
 * ============================================================
 * Deliberately small and unambiguous only -- e.g. NOT "TSE" (Tokyo
 * Stock Exchange vs Toronto Stock Exchange are both colloquially
 * "TSE"), NOT "SSE" (Shanghai vs a half-dozen others). Anything not
 * listed here must be given as the exact MIC code.
 */
typedef struct { const char *alias; const char *mic; } ExchangeAlias;
static const ExchangeAlias g_aliases[] = {
    {"NYSE",    "XNYS"},
    {"NASDAQ",  "XNAS"},
    {"LSE",     "XLON"},
    {"LONDON",  "XLON"},
    {"TOKYO",   "XTKS"},
    {"TSX",     "XTSE"},   /* Toronto -- "TSE" itself deliberately not aliased */
    {"TORONTO", "XTSE"},
    {"HKEX",    "XHKG"},
    {"HONGKONG","XHKG"},
    {"SSE",     "XSHG"},   /* Shanghai Stock Exchange */
    {"SHANGHAI","XSHG"},
    {"SGX",     "XSES"},
    {"SINGAPORE","XSES"},
    {"KRX",     "XKRX"},
    {"EURONEXT_PARIS", "XPAR"},
    {"PARIS",   "XPAR"},
    {"XETRA",   "XETR"},
    {"FRANKFURT","XETR"},
    {"ASX",     "XASX"},
    {"SYDNEY",  "XASX"},
    {"SIX",     "XSWX"},
    {"ZURICH",  "XSWX"},
    {"BSE",     "XBOM"},   /* Bombay Stock Exchange */
    {"NSE",     "XNSE"},   /* National Stock Exchange of India */
};
#define CAL_ALIAS_COUNT (int)(sizeof(g_aliases) / sizeof(g_aliases[0]))

/* Resolve an alias or pass through a code that already looks like a
 * MIC (4 uppercase letters). Returns pointer into g_exchanges or NULL. */
static Exchange* resolve_exchange(const char *name) {
    if (!g_loaded || !name || !name[0]) return NULL;

    char up[32];
    size_t n = strlen(name);
    if (n >= sizeof(up)) return NULL;
    for (size_t i = 0; i < n; i++) up[i] = (char)toupper((unsigned char)name[i]);
    up[n] = '\0';

    for (int i = 0; i < CAL_ALIAS_COUNT; i++) {
        if (strcmp(up, g_aliases[i].alias) == 0) {
            for (int j = 0; j < g_exchange_count; j++)
                if (strcmp(g_exchanges[j].code, g_aliases[i].mic) == 0)
                    return &g_exchanges[j];
            return NULL; /* alias table references a code we didn't load */
        }
    }
    for (int j = 0; j < g_exchange_count; j++)
        if (strcmp(g_exchanges[j].code, up) == 0) return &g_exchanges[j];

    return NULL;
}

/* ============================================================
 * Minimal JSON extraction -- same scoped-substring idiom as
 * src/currency.c / pipeline/src/risk_filter.c.
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

static int extract_str(const char *json, const char *key, char *out, int outlen) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ':') p++;
    if (*p != '"') return 0;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < outlen - 1) {
        if (*p == '\\' && *(p + 1)) { p++; out[i++] = *p++; }
        else out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

/* Same object/array boundary walker as currency.c's next_array_object,
 * duplicated rather than shared across a translation unit boundary
 * that doesn't otherwise exist between these two small parsers. */
static char* next_array_object(const char **cursor) {
    const char *p = *cursor;
    while (*p && *p != '{' && *p != ']') p++;
    if (*p != '{') { *cursor = p; return NULL; }
    const char *start = p;
    int depth = 0;
    while (*p) {
        if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') { depth--; if (depth == 0) { p++; break; } }
        else if (*p == '"') {
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

/* Return a malloc'd substring covering the value (object or array)
 * following "key" in json, or NULL if key absent or value is
 * null/scalar. Used to scope holidays.explicit / .generated /
 * ad_hoc_closures / sessions before walking them as arrays, and to
 * scope regular_hours / extended_hours.pre_market / .after_hours
 * before extracting their open/close fields. */
static char* extract_block(const char *json, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ':') p++;
    char open_ch = *p, close_ch;
    if (open_ch == '{') close_ch = '}';
    else if (open_ch == '[') close_ch = ']';
    else return NULL; /* null or scalar */

    const char *start = p;
    int depth = 0;
    while (*p) {
        if (*p == open_ch) depth++;
        else if (*p == close_ch) { depth--; if (depth == 0) { p++; break; } }
        else if (*p == '"') {
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
    return obj;
}

/* "HH:MM" -> minutes since midnight, or -1 if NULL/malformed. */
static int parse_hhmm(const char *s) {
    if (!s || !s[0]) return -1;
    int h = -1, m = -1;
    if (sscanf(s, "%d:%d", &h, &m) != 2) return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}

static int cmp_holiday_date(const void *a, const void *b) {
    return strcmp(((const HolidayEntry *)a)->date, ((const HolidayEntry *)b)->date);
}

/* Parse one holidays.explicit[] / .generated[] / ad_hoc_closures[]
 * array (all three share the same {date,status,early_close_time}
 * shape) into ex->holidays, deduplicating by date (later arrays --
 * ad_hoc_closures -- take priority over generated, which takes
 * priority over explicit, matching "explicit is authoritative,
 * generated fills gaps" from the registry's own build.py). */
static void parse_holiday_array(Exchange *ex, const char *arr_block) {
    if (!arr_block) return;
    const char *cursor = arr_block + 1;
    char *obj;
    while ((obj = next_array_object(&cursor)) != NULL) {
        char date[11] = "", status[16] = "", ect[8] = "";
        extract_str(obj, "date", date, sizeof(date));
        extract_str(obj, "status", status, sizeof(status));
        extract_str(obj, "early_close_time", ect, sizeof(ect));
        free(obj);
        if (!date[0]) continue;

        int existing = -1;
        for (int i = 0; i < ex->holiday_count; i++) {
            if (strcmp(ex->holidays[i].date, date) == 0) { existing = i; break; }
        }
        int idx = existing;
        if (idx < 0) {
            if (ex->holiday_count >= CAL_MAX_HOLIDAYS) continue; /* dropped, capacity */
            idx = ex->holiday_count++;
        }
        strncpy(ex->holidays[idx].date, date, sizeof(ex->holidays[idx].date) - 1);
        ex->holidays[idx].is_closed = (strcmp(status, "closed") == 0);
        ex->holidays[idx].early_close_min =
            (strcmp(status, "early_close") == 0) ? parse_hhmm(ect) : -1;
    }
}

/* ============================================================
 * Loading one exchange object
 * ============================================================ */

static void load_one_exchange(const char *obj) {
    if (g_exchange_count >= CAL_MAX_EXCHANGES) return;
    Exchange ex;
    memset(&ex, 0, sizeof(ex));
    ex.has_pre = ex.has_after = ex.has_lunch = 0;
    ex.pre_open_min = ex.pre_close_min = -1;
    ex.after_open_min = ex.after_close_min = -1;
    ex.lunch_open_min = ex.lunch_close_min = -1;

    if (!extract_str(obj, "code", ex.code, sizeof(ex.code))) return;
    extract_str(obj, "name", ex.name, sizeof(ex.name));
    extract_str(obj, "timezone", ex.timezone, sizeof(ex.timezone));
    if (!ex.timezone[0]) return; /* unusable without a timezone */

    char *rh = extract_block(obj, "regular_hours");
    if (rh) {
        char o[6] = "", c[6] = "";
        extract_str(rh, "open", o, sizeof(o));
        extract_str(rh, "close", c, sizeof(c));
        ex.regular_open_min  = parse_hhmm(o);
        ex.regular_close_min = parse_hhmm(c);
        free(rh);
    }
    if (ex.regular_open_min < 0 || ex.regular_close_min < 0) return; /* unusable */

    char *xh = extract_block(obj, "extended_hours");
    if (xh) {
        char *pm = extract_block(xh, "pre_market");
        if (pm) {
            char o[6] = "", c[6] = "";
            extract_str(pm, "open", o, sizeof(o));
            extract_str(pm, "close", c, sizeof(c));
            ex.pre_open_min = parse_hhmm(o);
            ex.pre_close_min = parse_hhmm(c);
            ex.has_pre = (ex.pre_open_min >= 0 && ex.pre_close_min >= 0);
            free(pm);
        }
        char *ah = extract_block(xh, "after_hours");
        if (ah) {
            char o[6] = "", c[6] = "";
            extract_str(ah, "open", o, sizeof(o));
            extract_str(ah, "close", c, sizeof(c));
            ex.after_open_min = parse_hhmm(o);
            ex.after_close_min = parse_hhmm(c);
            ex.has_after = (ex.after_open_min >= 0 && ex.after_close_min >= 0);
            free(ah);
        }
        free(xh);
    }

    /* sessions[] holds both "lunch_break" (a range) and "auction"
     * (a single instant) entries -- only lunch_break affects the
     * status vocabulary this library implements. */
    char *sessions = extract_block(obj, "sessions");
    if (sessions) {
        const char *cursor = sessions + 1;
        char *sobj;
        while ((sobj = next_array_object(&cursor)) != NULL) {
            char type[16] = "";
            extract_str(sobj, "type", type, sizeof(type));
            if (strcmp(type, "lunch_break") == 0) {
                char o[6] = "", c[6] = "";
                extract_str(sobj, "open", o, sizeof(o));
                extract_str(sobj, "close", c, sizeof(c));
                ex.lunch_open_min = parse_hhmm(o);
                ex.lunch_close_min = parse_hhmm(c);
                ex.has_lunch = (ex.lunch_open_min >= 0 && ex.lunch_close_min >= 0);
            }
            free(sobj);
        }
        free(sessions);
    }

    char *holidays_obj = extract_block(obj, "holidays");
    if (holidays_obj) {
        char *explicit_arr = extract_block(holidays_obj, "explicit");
        char *generated_arr = extract_block(holidays_obj, "generated");
        parse_holiday_array(&ex, explicit_arr);
        parse_holiday_array(&ex, generated_arr);
        free(explicit_arr);
        free(generated_arr);
        free(holidays_obj);
    }
    char *adhoc_arr = extract_block(obj, "ad_hoc_closures");
    parse_holiday_array(&ex, adhoc_arr);
    free(adhoc_arr);

    qsort(ex.holidays, (size_t)ex.holiday_count, sizeof(HolidayEntry), cmp_holiday_date);

    char *gr = extract_block(obj, "generation_range");
    if (gr) {
        /* generation_range is a plain 2-element string array, not an
         * array of objects -- extract the two quoted strings directly
         * rather than reusing next_array_object(). */
        const char *p = gr + 1;
        while (*p == ' ' || *p == '\n') p++;
        if (*p == '"') {
            p++;
            int i = 0;
            while (*p && *p != '"' && i < 10) ex.gen_range_start[i++] = *p++;
            ex.gen_range_start[i] = '\0';
        }
        const char *q = strchr(p, ',');
        if (q) {
            q++;
            while (*q == ' ' || *q == '\n') q++;
            if (*q == '"') {
                q++;
                int i = 0;
                while (*q && *q != '"' && i < 10) ex.gen_range_end[i++] = *q++;
                ex.gen_range_end[i] = '\0';
            }
        }
        free(gr);
    }

    g_exchanges[g_exchange_count++] = ex;
}

static int load_from_file(const char *path) {
    char *json = slurp_file(path);
    if (!json) return -1;

    char *exchanges_arr = extract_block(json, "exchanges");
    if (!exchanges_arr) {
        fprintf(stderr, "[calendar] ERROR: %s has no top-level 'exchanges' array "
                        "-- malformed or incompatible schema.\n", path);
        free(json);
        return -1;
    }

    const char *cursor = exchanges_arr + 1;
    g_exchange_count = 0;
    char *obj;
    while (g_exchange_count < CAL_MAX_EXCHANGES &&
           (obj = next_array_object(&cursor)) != NULL) {
        load_one_exchange(obj);
        free(obj);
    }
    free(exchanges_arr);
    free(json);

    if (g_exchange_count == 0) {
        fprintf(stderr, "[calendar] ERROR: %s parsed but yielded zero usable "
                        "exchanges.\n", path);
        return -1;
    }
    return 0;
}

static void install_builtin_nyse_fallback(void) {
    g_exchange_count = 0;
    Exchange *ex = &g_exchanges[g_exchange_count++];
    memset(ex, 0, sizeof(*ex));
    strcpy(ex->code, "XNYS");
    strcpy(ex->name, "New York Stock Exchange");
    strcpy(ex->timezone, "America/New_York");
    ex->regular_open_min  = 9 * 60 + 30;
    ex->regular_close_min = 16 * 60;
    ex->has_pre = 1;  ex->pre_open_min = 4 * 60;    ex->pre_close_min = 9 * 60 + 30;
    ex->has_after = 1; ex->after_open_min = 16 * 60; ex->after_close_min = 20 * 60;
    ex->holiday_count = 0; /* no holiday data in the fallback -- documented gap */
    fprintf(stderr,
        "[calendar] WARNING: calendar.json not found or unreadable -- using "
        "built-in NYSE-only defaults (regular/pre/after hours only, NO "
        "holiday data). Set LAS_SHELL_HOME or pass an explicit path to "
        "calendar_init() to load the full registry.\n");
}

int calendar_init(const char *path) {
    if (path && path[0] && access(path, R_OK) == 0) {
        if (load_from_file(path) == 0) { g_loaded = 1; return 0; }
    }
    const char *home = getenv("LAS_SHELL_HOME");
    if (home && home[0]) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s/calendar.json", home);
        if (access(buf, R_OK) == 0 && load_from_file(buf) == 0) {
            g_loaded = 1;
            return 0;
        }
    }
    if (access("/usr/local/share/las_shell/calendar.json", R_OK) == 0 &&
        load_from_file("/usr/local/share/las_shell/calendar.json") == 0) {
        g_loaded = 1;
        return 0;
    }
    install_builtin_nyse_fallback();
    g_loaded = 1;
    return 0;
}

void calendar_cleanup(void) {
    g_exchange_count = 0;
    g_loaded = 0;
}

/* ============================================================
 * Timezone conversion (see calendar.h for the tradeoff notes)
 * ============================================================ */

/* strncpy that always NUL-terminates -- named _local to avoid any
 * ambiguity with the codebase's existing safe_strncpy() from
 * my_own_shell.h, which this file intentionally does not include
 * (calendar.c has no other dependency on the rest of the shell). */
static void safe_strncpy_local(char *dst, const char *src, size_t n) {
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

/* Convert a UTC time_t to exchange-local y/m/d/hh/mm/wday.
 * wday: 0=Sunday .. 6=Saturday (struct tm convention). */
static void utc_to_local(const char *tz, time_t t,
                          int *y, int *mo, int *d, int *hh, int *mm, int *wday) {
    pthread_mutex_lock(&g_tz_mutex);
    char *old_tz_val = getenv("TZ");
    char old_tz[CAL_TZ_LEN] = "";
    int had_old = 0;
    if (old_tz_val) { safe_strncpy_local(old_tz, old_tz_val, sizeof(old_tz)); had_old = 1; }

    setenv("TZ", tz, 1);
    tzset();
    struct tm result;
    localtime_r(&t, &result);
    *y = result.tm_year + 1900;
    *mo = result.tm_mon + 1;
    *d = result.tm_mday;
    *hh = result.tm_hour;
    *mm = result.tm_min;
    *wday = result.tm_wday;

    if (had_old) setenv("TZ", old_tz, 1); else unsetenv("TZ");
    tzset();
    pthread_mutex_unlock(&g_tz_mutex);
}

/* Convert an exchange-local y/m/d/hh/mm to a UTC time_t. */
static time_t local_to_utc(const char *tz, int y, int mo, int d, int hh, int mm) {
    pthread_mutex_lock(&g_tz_mutex);
    char *old_tz_val = getenv("TZ");
    char old_tz[CAL_TZ_LEN] = "";
    int had_old = 0;
    if (old_tz_val) { safe_strncpy_local(old_tz, old_tz_val, sizeof(old_tz)); had_old = 1; }

    setenv("TZ", tz, 1);
    tzset();
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = hh;
    t.tm_min = mm;
    t.tm_sec = 0;
    t.tm_isdst = -1; /* let mktime resolve DST from the zone rules */
    time_t result = mktime(&t);

    if (had_old) setenv("TZ", old_tz, 1); else unsetenv("TZ");
    tzset();
    pthread_mutex_unlock(&g_tz_mutex);
    return result;
}

/* Pure calendar-date weekday (no timezone dependency): noon-UTC
 * trick avoids any DST-transition edge case in the day-of-week
 * computation itself. 0=Sunday..6=Saturday. */
static int weekday_of_date(int y, int mo, int d) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = 12;
    time_t utc = timegm(&t);
    struct tm out;
    gmtime_r(&utc, &out);
    return out.tm_wday;
}

static void format_date(char *buf, int buflen, int y, int mo, int d) {
    snprintf(buf, (size_t)buflen, "%04d-%02d-%02d", y, mo, d);
}

/* Add `days` calendar days to a y/m/d date (handles month/year
 * rollover via timegm/gmtime rather than hand-rolled month-length
 * arithmetic). */
static void add_days(int *y, int *mo, int *d, int days) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = *y - 1900;
    t.tm_mon = *mo - 1;
    t.tm_mday = *d + days;
    t.tm_hour = 12;
    time_t utc = timegm(&t);
    struct tm out;
    gmtime_r(&utc, &out);
    *y = out.tm_year + 1900;
    *mo = out.tm_mon + 1;
    *d = out.tm_mday;
}

/* ============================================================
 * Holiday lookup + per-day schedule resolution
 * ============================================================ */

static const HolidayEntry* find_holiday(const Exchange *ex, const char *date) {
    int lo = 0, hi = ex->holiday_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int c = strcmp(ex->holidays[mid].date, date);
        if (c == 0) return &ex->holidays[mid];
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

typedef struct {
    int is_full_holiday;      /* weekend or holiday status=="closed" */
    int early_close_min;      /* -1 if not an early-close day */
    int effective_close_min;  /* regular_close_min, or early_close_min if set */
} DaySchedule;

static void compute_day_schedule(const Exchange *ex, int y, int mo, int d,
                                  DaySchedule *out) {
    char date[11];
    format_date(date, sizeof(date), y, mo, d);
    int wd = weekday_of_date(y, mo, d);

    out->is_full_holiday = (wd == 0 || wd == 6);
    out->early_close_min = -1;

    if (!out->is_full_holiday) {
        const HolidayEntry *h = find_holiday(ex, date);
        if (h) {
            if (h->is_closed) out->is_full_holiday = 1;
            else if (h->early_close_min >= 0) out->early_close_min = h->early_close_min;
        }
    }
    out->effective_close_min = (out->early_close_min >= 0)
                                ? out->early_close_min : ex->regular_close_min;
}

/* ============================================================
 * Public API
 * ============================================================ */

int calendar_exchange_exists(const char *exchange) {
    return resolve_exchange(exchange) != NULL;
}

const char* calendar_status(const char *exchange) {
    static char result[16];
    strcpy(result, "closed");

    Exchange *ex = resolve_exchange(exchange);
    if (!ex) return result;

    time_t now = time(NULL);
    int y, mo, d, hh, mm, wd;
    utc_to_local(ex->timezone, now, &y, &mo, &d, &hh, &mm, &wd);

    DaySchedule sched;
    compute_day_schedule(ex, y, mo, d, &sched);
    if (sched.is_full_holiday) return result;

    int cur = hh * 60 + mm;

    if (ex->has_pre && cur >= ex->pre_open_min && cur < ex->pre_close_min) {
        strcpy(result, "pre");
        return result;
    }
    if (ex->has_lunch && cur >= ex->lunch_open_min && cur < ex->lunch_close_min) {
        strcpy(result, "lunch_break");
        return result;
    }
    if (cur >= ex->regular_open_min && cur < sched.effective_close_min) {
        strcpy(result, sched.early_close_min >= 0 ? "early_close" : "open");
        return result;
    }
    if (ex->has_after && cur >= ex->after_open_min && cur < ex->after_close_min) {
        strcpy(result, "after");
        return result;
    }
    return result; /* "closed" */
}

int calendar_is_open(const char *exchange) {
    const char *s = calendar_status(exchange);
    return (strcmp(s, "open") == 0 || strcmp(s, "early_close") == 0);
}

int calendar_minutes_until_change(const char *exchange) {
    Exchange *ex = resolve_exchange(exchange);
    if (!ex) return -1;

    time_t now = time(NULL);
    int y, mo, d, hh, mm, wd;
    utc_to_local(ex->timezone, now, &y, &mo, &d, &hh, &mm, &wd);
    int cur = hh * 60 + mm;

    DaySchedule sched;
    compute_day_schedule(ex, y, mo, d, &sched);

    if (!sched.is_full_holiday) {
        /* Collect today's remaining boundaries in chronological order
         * and take the first one still ahead of `cur`. */
        int boundaries[8];
        int nb = 0;
        if (ex->has_pre)             boundaries[nb++] = ex->pre_open_min;
        if (ex->has_pre)             boundaries[nb++] = ex->pre_close_min;
        if (ex->has_lunch)           boundaries[nb++] = ex->lunch_open_min;
        if (ex->has_lunch)           boundaries[nb++] = ex->lunch_close_min;
        boundaries[nb++] = ex->regular_open_min;
        boundaries[nb++] = sched.effective_close_min;
        if (ex->has_after)           boundaries[nb++] = ex->after_open_min;
        if (ex->has_after)           boundaries[nb++] = ex->after_close_min;

        int best = -1;
        for (int i = 0; i < nb; i++) {
            if (boundaries[i] > cur && (best < 0 || boundaries[i] < best))
                best = boundaries[i];
        }
        if (best >= 0) return best - cur;
    }

    /* Nothing left today (or today is a full holiday) -- roll forward
     * to the next trading day's first session boundary (pre-market
     * open if the exchange has one, else regular open). Capped at a
     * generous 30 days so a data/schema problem can't spin forever;
     * real holiday gaps are never remotely that long. */
    int ry = y, rmo = mo, rd = d;
    int minutes_to_midnight = 1440 - cur;
    for (int step = 1; step <= 30; step++) {
        add_days(&ry, &rmo, &rd, 1);
        DaySchedule next_sched;
        compute_day_schedule(ex, ry, rmo, rd, &next_sched);
        if (!next_sched.is_full_holiday) {
            int first_boundary = ex->has_pre ? ex->pre_open_min : ex->regular_open_min;
            return minutes_to_midnight + (step - 1) * 1440 + first_boundary;
        }
    }
    return -1; /* exceeded the search cap -- treat as an error, not a guess */
}

int calendar_is_holiday(const char *exchange, const char *date_iso) {
    Exchange *ex = resolve_exchange(exchange);
    if (!ex || !date_iso) return 0;
    int y, mo, d;
    if (sscanf(date_iso, "%d-%d-%d", &y, &mo, &d) != 3) return 0;
    DaySchedule sched;
    compute_day_schedule(ex, y, mo, d, &sched);
    return sched.is_full_holiday;
}

int calendar_is_early_close(const char *exchange, const char *date_iso) {
    Exchange *ex = resolve_exchange(exchange);
    if (!ex || !date_iso) return 0;
    int y, mo, d;
    if (sscanf(date_iso, "%d-%d-%d", &y, &mo, &d) != 3) return 0;
    DaySchedule sched;
    compute_day_schedule(ex, y, mo, d, &sched);
    return (!sched.is_full_holiday && sched.early_close_min >= 0);
}

const char* calendar_early_close_time(const char *exchange, const char *date_iso) {
    static char buf[8];
    Exchange *ex = resolve_exchange(exchange);
    if (!ex || !date_iso) return NULL;
    int y, mo, d;
    if (sscanf(date_iso, "%d-%d-%d", &y, &mo, &d) != 3) return NULL;
    DaySchedule sched;
    compute_day_schedule(ex, y, mo, d, &sched);
    if (sched.is_full_holiday || sched.early_close_min < 0) return NULL;
    int hh = (sched.early_close_min / 60) % 24;
    int mm = sched.early_close_min % 60;
    snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
    return buf;
}

static time_t find_next_boundary(const Exchange *ex, int want_open) {
    time_t now = time(NULL);
    int y, mo, d, hh, mm, wd;
    utc_to_local(ex->timezone, now, &y, &mo, &d, &hh, &mm, &wd);
    int cur = hh * 60 + mm;

    for (int step = 0; step <= 400; step++) {
        int ty = y, tmo = mo, td = d;
        if (step > 0) add_days(&ty, &tmo, &td, step);

        /* generation_range bound check: don't claim an open/close time
         * beyond what the registry actually has holiday data for. */
        char date[11];
        format_date(date, sizeof(date), ty, tmo, td);
        if (ex->gen_range_end[0] && strcmp(date, ex->gen_range_end) > 0) return 0;

        DaySchedule sched;
        compute_day_schedule(ex, ty, tmo, td, &sched);
        if (sched.is_full_holiday) continue;

        int target_min = want_open ? ex->regular_open_min : sched.effective_close_min;
        if (step == 0 && target_min <= cur) continue; /* already passed today */

        return local_to_utc(ex->timezone, ty, tmo, td, target_min / 60, target_min % 60);
    }
    return 0;
}

time_t calendar_next_open_time(const char *exchange) {
    Exchange *ex = resolve_exchange(exchange);
    if (!ex) return 0;
    return find_next_boundary(ex, 1);
}

time_t calendar_next_close_time(const char *exchange) {
    Exchange *ex = resolve_exchange(exchange);
    if (!ex) return 0;
    return find_next_boundary(ex, 0);
}

void calendar_list_exchanges(void) {
    if (!g_loaded) {
        fprintf(stderr, "[calendar] not initialized\n");
        return;
    }
    fprintf(stderr, "%-6s  %-40s  %s\n", "CODE", "NAME", "TIMEZONE");
    for (int i = 0; i < g_exchange_count; i++) {
        fprintf(stderr, "%-6s  %-40s  %s\n",
                g_exchanges[i].code, g_exchanges[i].name, g_exchanges[i].timezone);
    }
    fprintf(stderr, "(%d exchanges)\n", g_exchange_count);
}
