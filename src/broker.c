/*
 * broker.c — Las_shell Phase 4.2: Broker API Bridge
 * ═══════════════════════════════════════════════
 *
 * Provides built-in commands that speak to broker REST APIs.
 * Supports IBKR (Client Portal Gateway), Alpaca v2, and any custom
 * endpoint set via $BROKER_API.
 *
 * In PAPER mode (ACCOUNT=PAPER):
 *   - If BROKER_API is set  → HTTP to that endpoint (sim_server.py).
 *   - If BROKER_API unset   → in-process ledger (fast, zero network deps).
 *
 * In LIVE mode (ACCOUNT != PAPER):
 *   - Requires libcurl (detected at compile time via LAS_SHELL_HAVE_CURL).
 *   - WITHOUT curl the build hard-errors — we never silently fake a live order.
 *
 * Broker adapters
 * ────────────────
 *   BROKER=ALPACA  → Alpaca Markets v2 REST  (api.alpaca.markets)
 *   BROKER=IBKR    → IB Client Portal Gateway v1
 *   BROKER=<other> → Generic REST (Alpaca-compatible JSON schema)
 *
 * Runtime files
 * ─────────────
 *   ~/.las_shell_paper_account   key=value ledger (persisted)
 *   ~/.las_shell_order_log       CSV append: every fill / rejection
 *   ~/.las_shell_pnl             single float for the prompt daemon
 *
 * Compile-time flags
 * ──────────────────
 *   -DLAS_SHELL_HAVE_CURL        enable libcurl live path
 *   -DLAS_SHELL_CURL_VERBOSE     curl verbose (debug)
 */

#define _POSIX_C_SOURCE 200809L

#include "../include/my_own_shell.h"
#include "../include/broker.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <strings.h>    /* strcasecmp on POSIX */

#ifdef LAS_SHELL_HAVE_CURL
#  include <curl/curl.h>
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * §1  Constants & Internal Types
 * ═══════════════════════════════════════════════════════════════════════ */

#define MAX_POSITIONS       256
#define MAX_TICKER_LEN       16
#define ORDER_LOG_HEADER     "timestamp,action,ticker,qty,fill_price,notional,status,order_id,broker\n"
#define PAPER_ACCOUNT_MAGIC  "# Las_shell paper account v2 — auto-generated\n"

typedef enum { TIF_DAY = 0, TIF_GTC, TIF_IOC, TIF_FOK } TimeInForce;
typedef enum { OTYPE_MARKET = 0, OTYPE_LIMIT, OTYPE_STOP, OTYPE_STOP_LIMIT } OrderType;

/* Result returned by every adapter */
typedef struct {
    int    ok;
    long   order_id;
    double fill_price;
    char   message[256];
} BrokerResult;

/* One position in the paper ledger */
typedef struct {
    char   ticker[MAX_TICKER_LEN];
    int    quantity;        /* +long / -short */
    double avg_cost;
    double realized_pnl;
} Position;

/* In-process paper account */
static struct {
    double    cash;
    double    starting_cash;
    Position  positions[MAX_POSITIONS];
    int       pos_count;
    long      order_seq;
    int       loaded;
} g_paper = {
    .cash          = 100000.0,
    .starting_cash = 100000.0,
    .pos_count     = 0,
    .order_seq     = 1000,
    .loaded        = 0
};

/* Cached paths (built once, then reused) */
static char g_paper_file[512];
static char g_order_log[512];
static char g_pnl_file[512];

/* ═══════════════════════════════════════════════════════════════════════
 * §2  Utility helpers
 * ═══════════════════════════════════════════════════════════════════════ */

static char *ticker_upper(char *s) {
    for (char *p = s; *p; p++) *p = (char)toupper((unsigned char)*p);
    return s;
}

/* Build $HOME/<suffix>, cache in *dest. */
static void home_path(char *dest, size_t len, const char *suffix) {
    if (dest[0]) return;
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(dest, len, "%s/%s", home, suffix);
}

/* Write ISO-8601 UTC timestamp into buf (≥25 bytes). */
static void iso_now(char *buf, size_t len) {
    time_t t = time(NULL);
    struct tm *u = gmtime(&t);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", u);
}

/* Append one record to ~/.las_shell_order_log. */
static void order_log_append(const char *action, const char *ticker,
                              int qty, double fill, const char *status,
                              long oid, const char *broker_name)
{
    home_path(g_order_log, sizeof(g_order_log), ".las_shell_order_log");
    int fd = open(g_order_log, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;

    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size == 0)
        write(fd, ORDER_LOG_HEADER, strlen(ORDER_LOG_HEADER));

    char ts[32];
    iso_now(ts, sizeof(ts));

    char rec[512];
    int n = snprintf(rec, sizeof(rec),
        "%s,%s,%s,%d,%.4f,%.2f,%s,%ld,%s\n",
        ts, action, ticker, qty,
        fill, fill * qty,
        status, oid,
        broker_name ? broker_name : "paper");
    write(fd, rec, (size_t)n);
    close(fd);
}

/* Write P&L float to ~/.las_shell_pnl for the prompt daemon. */
static void write_pnl_file(double pnl) {
    home_path(g_pnl_file, sizeof(g_pnl_file), ".las_shell_pnl");
    FILE *f = fopen(g_pnl_file, "w");
    if (!f) return;
    fprintf(f, "%.2f\n", pnl);
    fclose(f);
}

/* ═══════════════════════════════════════════════════════════════════════
 * §3  Price feed  (delegates to scripts/quote.py)
 * ═══════════════════════════════════════════════════════════════════════ */

static double get_market_price(const char *ticker, char **env) {
    char cmd[512];
    const char *lhome = getenv("LAS_SHELL_HOME");
    if (!lhome || lhome[0] == '\0') lhome = "/usr/local/share/las_shell";
    
    snprintf(cmd, sizeof(cmd), "python3 %s/scripts/quote.py %s 2>/dev/null", 
             lhome, ticker);

    int pfd[2];
    if (pipe(pfd) != 0) return 100.0;

    pid_t pid = fork();
    if (pid == 0) {
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        if (env) { extern char **environ; environ = env; }
        char *a[] = {"/bin/sh", "-c", cmd, NULL};
        execvp("/bin/sh", a);
        _exit(1);
    }
    close(pfd[1]);

    char buf[64] = {0};
    ssize_t n = read(pfd[0], buf, sizeof(buf) - 1);
    close(pfd[0]);
    waitpid(pid, NULL, 0);

    if (n <= 0) {
        fprintf(stderr, "[broker] WARNING: no price data for %s — using $100.00 fallback\n", ticker);
        return 100.0;
    }
    double price = 100.0;
    char *sp = strchr(buf, ' ');
    if (sp) sscanf(sp, " %lf", &price);
    else    sscanf(buf, " %lf", &price);
    if (price <= 0.0) {
        fprintf(stderr, "[broker] WARNING: invalid price for %s — using $100.00 fallback\n", ticker);
        return 100.0;
    }
    return price;}

/* ═══════════════════════════════════════════════════════════════════════
 * §4  Paper account — in-process ledger
 * ═══════════════════════════════════════════════════════════════════════ */

static void paper_ensure_path(void) {
    home_path(g_paper_file, sizeof(g_paper_file), ".las_shell_paper_account");
}

static void paper_load(void) {
    if (g_paper.loaded) return;
    paper_ensure_path();
    g_paper.loaded = 1;

    FILE *f = fopen(g_paper_file, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char key[64] = {0}, val[128] = {0};
        if (sscanf(line, "%63[^=]=%127[^\n]", key, val) != 2) continue;

        if      (!strcmp(key, "cash"))          g_paper.cash          = atof(val);
        else if (!strcmp(key, "starting_cash")) g_paper.starting_cash = atof(val);
        else if (!strcmp(key, "order_seq"))     g_paper.order_seq     = atol(val);
        else if (!strncmp(key, "pos.", 4)) {
            if (g_paper.pos_count >= MAX_POSITIONS) continue;
            Position *p = &g_paper.positions[g_paper.pos_count];
            strncpy(p->ticker, key + 4, sizeof(p->ticker) - 1);
            if (sscanf(val, "%d:%lf:%lf",
                       &p->quantity, &p->avg_cost, &p->realized_pnl) == 3)
                if (p->quantity != 0) g_paper.pos_count++;
        }
    }
    fclose(f);
}

static void paper_save(void) {
    paper_ensure_path();
    FILE *f = fopen(g_paper_file, "w");
    if (!f) { perror("broker: cannot save paper account"); return; }

    fputs(PAPER_ACCOUNT_MAGIC, f);
    fprintf(f, "cash=%.4f\n",          g_paper.cash);
    fprintf(f, "starting_cash=%.4f\n", g_paper.starting_cash);
    fprintf(f, "order_seq=%ld\n",      g_paper.order_seq);
    for (int i = 0; i < g_paper.pos_count; i++) {
        Position *p = &g_paper.positions[i];
        if (p->quantity == 0) continue;
        fprintf(f, "pos.%s=%d:%.4f:%.4f\n",
                p->ticker, p->quantity, p->avg_cost, p->realized_pnl);
    }
    fclose(f);
}

/* Find or create a Position slot (NULL if ledger full). */
static Position *paper_find_or_create(const char *ticker) {
    for (int i = 0; i < g_paper.pos_count; i++)
        if (!strcmp(g_paper.positions[i].ticker, ticker))
            return &g_paper.positions[i];
    if (g_paper.pos_count >= MAX_POSITIONS) return NULL;
    Position *p = &g_paper.positions[g_paper.pos_count++];
    memset(p, 0, sizeof(*p));
    strncpy(p->ticker, ticker, sizeof(p->ticker) - 1);
    return p;
}

static BrokerResult paper_execute_buy(const char *ticker, int qty,
                                      double limit, OrderType otype,
                                      char **env)
{
    BrokerResult r = {0};
    paper_load();

    double fill = (otype == OTYPE_MARKET || limit <= 0.0)
                  ? get_market_price(ticker, env) : limit;
    double notional = fill * qty;

    if (g_paper.cash < notional) {
        snprintf(r.message, sizeof(r.message),
                 "REJECTED — insufficient cash (need $%.2f, have $%.2f)",
                 notional, g_paper.cash);
        /* BUG-B FIX: do NOT increment order_seq for rejected orders.
         * Rejections are logged with order_id=0 to distinguish them
         * from fills in the order log.                               */
        order_log_append("buy", ticker, qty, fill, "REJECTED_CASH",
                         0, "paper");
        return r;
    }

    Position *pos = paper_find_or_create(ticker);
    if (!pos) {
        snprintf(r.message, sizeof(r.message),
                 "REJECTED — ledger full (max %d positions)", MAX_POSITIONS);
        return r;
    }

    g_paper.cash -= notional;
    double old_cost  = pos->avg_cost * pos->quantity;
    pos->quantity   += qty;
    pos->avg_cost    = pos->quantity > 0
                       ? (old_cost + notional) / pos->quantity : fill;

    r.ok = 1; r.order_id = ++g_paper.order_seq; r.fill_price = fill;
    snprintf(r.message, sizeof(r.message), "FILLED @ $%.4f  cash_remaining=$%.2f",
             fill, g_paper.cash);

    paper_save();
    order_log_append("buy", ticker, qty, fill, "FILLED", r.order_id, "paper");
    return r;
}

static BrokerResult paper_execute_sell(const char *ticker, int qty,
                                       double limit, OrderType otype,
                                       char **env)
{
    BrokerResult r = {0};
    paper_load();

    Position *pos = NULL;
    for (int i = 0; i < g_paper.pos_count; i++)
        if (!strcmp(g_paper.positions[i].ticker, ticker))
            { pos = &g_paper.positions[i]; break; }

    if (!pos || pos->quantity < qty) {
        snprintf(r.message, sizeof(r.message),
                 "REJECTED — insufficient position in %s (have %d, selling %d)",
                 ticker, pos ? pos->quantity : 0, qty);
        /* BUG-B FIX: do NOT increment order_seq for rejected orders. */
        order_log_append("sell", ticker, qty, 0.0, "REJECTED_POS",
                         0, "paper");
        return r;
    }

    double fill = (otype == OTYPE_MARKET || limit <= 0.0)
                  ? get_market_price(ticker, env) : limit;

    pos->realized_pnl += (fill - pos->avg_cost) * qty;
    pos->quantity     -= qty;
    g_paper.cash      += fill * qty;

    r.ok = 1; r.order_id = ++g_paper.order_seq; r.fill_price = fill;
    snprintf(r.message, sizeof(r.message),
             "FILLED @ $%.4f  realized_pnl=%+.2f  cash=$%.2f",
             fill, pos->realized_pnl, g_paper.cash);

    paper_save();
    order_log_append("sell", ticker, qty, fill, "FILLED", r.order_id, "paper");
    return r;
}

static int paper_print_positions(int json, const char *filter, char **env) {
    paper_load();

    int live = 0;
    for (int i = 0; i < g_paper.pos_count; i++)
        if (g_paper.positions[i].quantity != 0) live++;

    if (live == 0) { puts(json ? "[]" : "No open positions."); return 0; }

    if (json) {
        puts("[");
        int first = 1;
        for (int i = 0; i < g_paper.pos_count; i++) {
            Position *p = &g_paper.positions[i];
            if (!p->quantity) continue;
            if (filter && strcmp(p->ticker, filter)) continue;
            double mkt  = get_market_price(p->ticker, env);
            if (!first) puts(",");
            first = 0;
            printf("  {\"symbol\":\"%s\",\"qty\":%d,\"avg_cost\":%.4f,"
                   "\"market_price\":%.4f,\"market_value\":%.2f,"
                   "\"unrealized_pnl\":%.2f,\"realized_pnl\":%.2f}",
                   p->ticker, p->quantity, p->avg_cost,
                   mkt, mkt * p->quantity,
                   (mkt - p->avg_cost) * p->quantity,
                   p->realized_pnl);
        }
        puts("\n]");
        return 0;
    }

    printf("\n  %-8s %8s %10s %12s %12s %12s\n",
           "TICKER","QTY","AVG_COST","MKT_PRICE","MKT_VAL","UNREAL_PNL");
    printf("  %s\n","──────────────────────────────────────────────────────────────");

    double tot_mv = 0, tot_up = 0, tot_rp = 0;
    for (int i = 0; i < g_paper.pos_count; i++) {
        Position *p = &g_paper.positions[i];
        if (!p->quantity) continue;
        if (filter && strcmp(p->ticker, filter)) continue;
        double mkt = get_market_price(p->ticker, env);
        double mv  = mkt * p->quantity;
        double up  = (mkt - p->avg_cost) * p->quantity;
        printf("  %-8s %8d %10.2f %12.2f %12.2f %+12.2f\n",
               p->ticker, p->quantity, p->avg_cost, mkt, mv, up);
        tot_mv += mv; tot_up += up; tot_rp += p->realized_pnl;
    }
    printf("  %s\n","──────────────────────────────────────────────────────────────");
    printf("  %-8s %8s %10s %12s %12.2f %+12.2f\n",
           "TOTAL","","","",tot_mv,tot_up);
    printf("  Realized P&L (closed positions): %+.2f\n\n", tot_rp);
    return 0;
}

static int paper_print_balance(int json, char **env) {
    paper_load();
    double tot_mkt = 0;
    for (int i = 0; i < g_paper.pos_count; i++) {
        Position *p = &g_paper.positions[i];
        if (!p->quantity) continue;
        tot_mkt += get_market_price(p->ticker, env) * p->quantity;
    }
    double equity  = g_paper.cash + tot_mkt;
    double pnl     = equity - g_paper.starting_cash;
    double pnl_pct = g_paper.starting_cash > 0
                     ? (pnl / g_paper.starting_cash) * 100.0 : 0.0;
    write_pnl_file(pnl);

    if (json) {
        printf("{\"mode\":\"PAPER\",\"cash\":%.2f,\"market_value\":%.2f,"
               "\"equity\":%.2f,\"total_pnl\":%.2f,\"pnl_pct\":%.4f,"
               "\"starting_cash\":%.2f}\n",
               g_paper.cash, tot_mkt, equity, pnl, pnl_pct,
               g_paper.starting_cash);
        return 0;
    }
    printf("\n  Account Balance  [PAPER MODE]\n");
    printf("  %s\n","──────────────────────────────────────────");
    printf("  Cash:            $%13.2f\n", g_paper.cash);
    printf("  Market Value:    $%13.2f\n", tot_mkt);
    printf("  Equity:          $%13.2f\n", equity);
    printf("  Starting Cash:   $%13.2f\n", g_paper.starting_cash);
    printf("  Total P&L:       $%+13.2f  (%+.2f%%)\n\n", pnl, pnl_pct);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * §5  libcurl HTTP layer
 * ═══════════════════════════════════════════════════════════════════════ */

#ifdef LAS_SHELL_HAVE_CURL

/* Growable response buffer for curl writes */
typedef struct { char *data; size_t len; size_t cap; } CurlBuf;

static size_t curl_write_cb(void *contents, size_t size, size_t nmemb,
                             void *userp)
{
    size_t real = size * nmemb;
    CurlBuf *b  = (CurlBuf *)userp;
    if (b->len + real + 1 > b->cap) {
        size_t nc = b->cap + real + 4096;
        char  *tmp = realloc(b->data, nc);
        if (!tmp) return 0;
        b->data = tmp; b->cap = nc;
    }
    memcpy(b->data + b->len, contents, real);
    b->len += real;
    b->data[b->len] = '\0';
    return real;
}

static CurlBuf *curlbuf_new(void) {
    CurlBuf *b = calloc(1, sizeof(CurlBuf));
    b->data = malloc(4096); b->cap = 4096; b->data[0] = '\0';
    return b;
}
static void curlbuf_free(CurlBuf *b) { if (b) { free(b->data); free(b); } }

/*
 * Minimal single-level JSON field extractor.
 * Returns malloc'd string (caller must free) or NULL.
 */
static char *json_field(const char *json, const char *field) {
    if (!json || !field) return NULL;
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\":", field);
    const char *p = strstr(json, needle);
    if (!p) return NULL;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') {
        p++;
        const char *e = strchr(p, '"');
        return e ? strndup(p, (size_t)(e - p)) : NULL;
    }
    const char *e = p;
    while (*e && *e != ',' && *e != '}' && *e != ']' && *e != ' ' && *e != '\n') e++;
    return strndup(p, (size_t)(e - p));
}

/* ── Alpaca v2 adapter ──────────────────────────────────────────────── */

static struct curl_slist *alpaca_hdrs(const char *api_key,
                                      const char *api_secret) {
    struct curl_slist *h = NULL;
    if (api_key && api_key[0]) {
        char kh[256], sh[256];
        snprintf(kh, sizeof(kh), "APCA-API-KEY-ID: %s",    api_key);
        snprintf(sh, sizeof(sh), "APCA-API-SECRET-KEY: %s", api_secret ? api_secret : "");
        h = curl_slist_append(h, kh);
        h = curl_slist_append(h, sh);
    }
    h = curl_slist_append(h, "Content-Type: application/json");
    h = curl_slist_append(h, "Accept: application/json");
    return h;
}

static BrokerResult alpaca_order(const char *action, const char *ticker,
                                  int qty, double limit,
                                  OrderType otype, TimeInForce tif,
                                  char **env)
{
    BrokerResult r = {0};
    const char *base   = my_getenv("BROKER_API",        env);
    const char *key    = my_getenv("ALPACA_API_KEY",    env);
    const char *secret = my_getenv("ALPACA_API_SECRET", env);
    if (!base || !base[0]) {
        snprintf(r.message, sizeof(r.message), "BROKER_API not set");
        return r;
    }

    const char *ts = (tif==TIF_GTC)?"gtc":(tif==TIF_IOC)?"ioc":(tif==TIF_FOK)?"fok":"day";
    const char *ty = (otype==OTYPE_LIMIT)?"limit":(otype==OTYPE_STOP)?"stop"
                   : (otype==OTYPE_STOP_LIMIT)?"stop_limit":"market";

    char body[512];
    if (otype == OTYPE_LIMIT || otype == OTYPE_STOP_LIMIT)
        snprintf(body, sizeof(body),
                 "{\"symbol\":\"%s\",\"qty\":\"%d\",\"side\":\"%s\","
                 "\"type\":\"%s\",\"time_in_force\":\"%s\","
                 "\"limit_price\":\"%.4f\"}", ticker,qty,action,ty,ts,limit);
    else
        snprintf(body, sizeof(body),
                 "{\"symbol\":\"%s\",\"qty\":\"%d\",\"side\":\"%s\","
                 "\"type\":\"%s\",\"time_in_force\":\"%s\"}",
                 ticker,qty,action,ty,ts);

    char url[512]; snprintf(url, sizeof(url), "%s/v2/orders", base);

    CURL *c = curl_easy_init(); if (!c) { strcpy(r.message,"curl init"); return r; }
    CurlBuf *buf = curlbuf_new(); long code = 0;
    struct curl_slist *hdrs = alpaca_hdrs(key, secret);

    curl_easy_setopt(c, CURLOPT_URL,           url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,    hdrs);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS,    body);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,     buf);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,       10L);
#ifdef LAS_SHELL_CURL_VERBOSE
    curl_easy_setopt(c, CURLOPT_VERBOSE,       1L);
#endif
    CURLcode res = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdrs); curl_easy_cleanup(c);

    if (res != CURLE_OK) {
        snprintf(r.message,sizeof(r.message),"curl: %s",curl_easy_strerror(res));
        curlbuf_free(buf); return r;
    }
    if (code == 200 || code == 201) {
        r.ok = 1;
        char *oid = json_field(buf->data,"id");
        char *fqty= json_field(buf->data,"filled_qty");
        char *favg= json_field(buf->data,"filled_avg_price");
        char *st  = json_field(buf->data,"status");
        r.order_id   = oid  ? atol(oid)  : 0;
        r.fill_price = favg ? atof(favg) : limit;
        snprintf(r.message,sizeof(r.message),
                 "id=%s filled_qty=%s fill_avg=%s status=%s",
                 oid?oid:"?", fqty?fqty:"pending",
                 favg?favg:"pending", st?st:"unknown");
        free(oid); free(fqty); free(favg); free(st);
        order_log_append(action,ticker,qty,r.fill_price,"FILLED",r.order_id,"alpaca");
    } else {
        char *msg = json_field(buf->data,"message");
        snprintf(r.message,sizeof(r.message),"HTTP %ld — %s",code,msg?msg:buf->data);
        free(msg);
        order_log_append(action,ticker,qty,0.0,"REJECTED_API",0,"alpaca");
    }
    curlbuf_free(buf); return r;
}

static int alpaca_positions(int json, const char *filter, char **env) {
    const char *base   = my_getenv("BROKER_API",        env);
    const char *key    = my_getenv("ALPACA_API_KEY",    env);
    const char *secret = my_getenv("ALPACA_API_SECRET", env);
    if (!base||!base[0]) { fputs("[broker] BROKER_API not set\n",stderr); return 1; }

    char url[512];
    if (filter && filter[0]) snprintf(url,sizeof(url),"%s/v2/positions/%s",base,filter);
    else                     snprintf(url,sizeof(url),"%s/v2/positions",base);

    CURL *c = curl_easy_init(); if (!c) return 1;
    CurlBuf *buf = curlbuf_new(); long code = 0;
    struct curl_slist *hdrs = alpaca_hdrs(key, secret);

    curl_easy_setopt(c, CURLOPT_URL,           url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,    hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,     buf);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,       10L);
    curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdrs); curl_easy_cleanup(c);

    if (code != 200) {
        fprintf(stderr,"[broker] positions HTTP %ld\n%s\n",code,buf->data);
        curlbuf_free(buf); return 1;
    }
    if (json) { puts(buf->data); curlbuf_free(buf); return 0; }

    /* Pretty-print: walk JSON array of objects */
    printf("\n  %-8s %8s %12s %12s %12s\n",
           "SYMBOL","QTY","AVG_ENTRY","MKT_VALUE","UNREAL_PNL");
    printf("  %s\n","──────────────────────────────────────────────────────");
    const char *p = buf->data; int found = 0;
    while ((p = strchr(p, '{')) != NULL) {
        const char *e = strchr(p, '}'); if (!e) break;
        char *obj = strndup(p, (size_t)(e-p)+1);
        char *sym  = json_field(obj,"symbol");
        char *qty  = json_field(obj,"qty");
        char *avg  = json_field(obj,"avg_entry_price");
        char *mv   = json_field(obj,"market_value");
        char *upnl = json_field(obj,"unrealized_pl");
        if (sym) {
            printf("  %-8s %8s %12s %12s %12s\n",
                   sym, qty?qty:"?", avg?avg:"?", mv?mv:"?", upnl?upnl:"?");
            found = 1;
        }
        free(sym);free(qty);free(avg);free(mv);free(upnl);free(obj);
        p = e + 1;
    }
    if (!found) puts("  No open positions.");
    puts("");
    curlbuf_free(buf); return 0;
}

static int alpaca_balance(int json, char **env) {
    const char *base   = my_getenv("BROKER_API",        env);
    const char *key    = my_getenv("ALPACA_API_KEY",    env);
    const char *secret = my_getenv("ALPACA_API_SECRET", env);
    if (!base||!base[0]) { fputs("[broker] BROKER_API not set\n",stderr); return 1; }

    char url[512]; snprintf(url,sizeof(url),"%s/v2/account",base);
    CURL *c = curl_easy_init(); if (!c) return 1;
    CurlBuf *buf = curlbuf_new(); long code = 0;
    struct curl_slist *hdrs = alpaca_hdrs(key, secret);

    curl_easy_setopt(c, CURLOPT_URL,           url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,    hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,     buf);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,       10L);
    curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdrs); curl_easy_cleanup(c);

    if (code != 200) {
        fprintf(stderr,"[broker] balance HTTP %ld\n%s\n",code,buf->data);
        curlbuf_free(buf); return 1;
    }
    if (json) { puts(buf->data); curlbuf_free(buf); return 0; }

    char *eq = json_field(buf->data,"equity");
    char *cash= json_field(buf->data,"cash");
    char *bp  = json_field(buf->data,"buying_power");
    char *st  = json_field(buf->data,"status");
    printf("\n  Account Balance  [ALPACA LIVE]\n");
    printf("  %s\n","──────────────────────────────────────────");
    printf("  Equity:        $%s\n", eq  ?eq  :"?");
    printf("  Cash:          $%s\n", cash?cash:"?");
    printf("  Buying Power:  $%s\n", bp  ?bp  :"?");
    printf("  Status:         %s\n\n",st  ?st  :"?");
    free(eq);free(cash);free(bp);free(st);
    curlbuf_free(buf); return 0;
}

static int alpaca_cancel(const char *oid, char **env) {
    const char *base   = my_getenv("BROKER_API",        env);
    const char *key    = my_getenv("ALPACA_API_KEY",    env);
    const char *secret = my_getenv("ALPACA_API_SECRET", env);
    if (!base||!base[0]) { fputs("[broker] BROKER_API not set\n",stderr); return 1; }

    char url[512]; snprintf(url,sizeof(url),"%s/v2/orders/%s",base,oid);
    CURL *c = curl_easy_init(); if (!c) return 1;
    CurlBuf *buf = curlbuf_new(); long code = 0;
    struct curl_slist *hdrs = alpaca_hdrs(key, secret);

    curl_easy_setopt(c, CURLOPT_URL,           url);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,    hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,     buf);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,       10L);
    curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdrs); curl_easy_cleanup(c);
    curlbuf_free(buf);

    if (code==200||code==204) { printf("[broker] Order %s cancelled.\n",oid); return 0; }
    fprintf(stderr,"[broker] cancel HTTP %ld\n",code); return 1;
}

/* ── IBKR Client Portal Gateway adapter ────────────────────────────── */

static BrokerResult ibkr_order(const char *action, const char *ticker,
                                int qty, double limit,
                                OrderType otype, TimeInForce tif,
                                char **env)
{
    BrokerResult r = {0};
    const char *base = my_getenv("BROKER_API",       env);
    const char *acct = my_getenv("IBKR_ACCOUNT_ID",  env);
    if (!base||!base[0]) { snprintf(r.message,sizeof(r.message),"BROKER_API not set"); return r; }
    if (!acct||!acct[0]) {
        snprintf(r.message,sizeof(r.message),
                 "IBKR_ACCOUNT_ID not set (setenv IBKR_ACCOUNT_ID <id>)"); return r;
    }

    const char *side = (!strcmp(action,"buy"))?"BUY":"SELL";
    const char *ty   = (otype==OTYPE_LIMIT)?"LMT":(otype==OTYPE_STOP)?"STP":"MKT";
    const char *tf   = (tif==TIF_GTC)?"GTC":(tif==TIF_IOC)?"IOC":"DAY";

    char body[512];
    if (otype==OTYPE_LIMIT)
        snprintf(body,sizeof(body),
                 "[{\"acctId\":\"%s\",\"conid\":0,\"secType\":\"%s:STK\","
                 "\"orderType\":\"%s\",\"side\":\"%s\",\"quantity\":%d,"
                 "\"tif\":\"%s\",\"price\":%.4f}]",
                 acct,ticker,ty,side,qty,tf,limit);
    else
        snprintf(body,sizeof(body),
                 "[{\"acctId\":\"%s\",\"conid\":0,\"secType\":\"%s:STK\","
                 "\"orderType\":\"%s\",\"side\":\"%s\",\"quantity\":%d,"
                 "\"tif\":\"%s\"}]",
                 acct,ticker,ty,side,qty,tf);

    char url[512]; snprintf(url,sizeof(url),
        "%s/v1/api/iserver/account/%s/orders",base,acct);

    CURL *c = curl_easy_init(); if (!c) { strcpy(r.message,"curl init"); return r; }
    CurlBuf *buf = curlbuf_new(); long code = 0;
    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs,"Content-Type: application/json");

    curl_easy_setopt(c, CURLOPT_URL,            url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,     hdrs);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS,     body);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,      buf);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,        15L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L); /* CPG uses self-signed */
    CURLcode res = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdrs); curl_easy_cleanup(c);

    if (res != CURLE_OK) {
        snprintf(r.message,sizeof(r.message),"curl: %s",curl_easy_strerror(res));
        curlbuf_free(buf); return r;
    }
    if (code==200) {
        char *oid = json_field(buf->data,"order_id");
        char *st  = json_field(buf->data,"order_status");
        r.ok=1; r.order_id=oid?atol(oid):0; r.fill_price=limit;
        snprintf(r.message,sizeof(r.message),
                 "IBKR order_id=%s status=%s",oid?oid:"?",st?st:"?");
        free(oid); free(st);
        order_log_append(action,ticker,qty,r.fill_price,"SUBMITTED",r.order_id,"ibkr");
    } else {
        char *err = json_field(buf->data,"error");
        snprintf(r.message,sizeof(r.message),"IBKR HTTP %ld — %s",
                 code, err?err:buf->data);
        free(err);
        order_log_append(action,ticker,qty,0.0,"REJECTED_API",0,"ibkr");
    }
    curlbuf_free(buf); return r;
}

/* ── Generic REST adapter (also handles ACCOUNT=PAPER + BROKER_API) ── */

static BrokerResult generic_order(const char *action, const char *ticker,
                                   int qty, double limit,
                                   OrderType otype, TimeInForce tif,
                                   char **env)
{
    BrokerResult r = {0};
    const char *base = my_getenv("BROKER_API", env);
    if (!base||!base[0]) {
        snprintf(r.message,sizeof(r.message),
                 "BROKER_API not set (setenv BROKER_API http://...)"); return r;
    }

    const char *ty = (otype==OTYPE_LIMIT)?"limit":(otype==OTYPE_STOP)?"stop"
                   : (otype==OTYPE_STOP_LIMIT)?"stop_limit":"market";
    const char *tf = (tif==TIF_GTC)?"gtc":(tif==TIF_IOC)?"ioc":(tif==TIF_FOK)?"fok":"day";

    char body[512];
    if (otype==OTYPE_LIMIT||otype==OTYPE_STOP_LIMIT)
        snprintf(body,sizeof(body),
                 "{\"symbol\":\"%s\",\"qty\":%d,\"side\":\"%s\","
                 "\"type\":\"%s\",\"time_in_force\":\"%s\","
                 "\"limit_price\":%.4f}",
                 ticker,qty,action,ty,tf,limit);
    else
        snprintf(body,sizeof(body),
                 "{\"symbol\":\"%s\",\"qty\":%d,\"side\":\"%s\","
                 "\"type\":\"%s\",\"time_in_force\":\"%s\"}",
                 ticker,qty,action,ty,tf);

    char url[512]; snprintf(url,sizeof(url),"%s/orders",base);
    CURL *c = curl_easy_init(); if (!c) { strcpy(r.message,"curl init"); return r; }
    CurlBuf *buf = curlbuf_new(); long code = 0;
    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs,"Content-Type: application/json");
    hdrs = curl_slist_append(hdrs,"Accept: application/json");

    curl_easy_setopt(c, CURLOPT_URL,           url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,    hdrs);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS,    body);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,     buf);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,       10L);
#ifdef LAS_SHELL_CURL_VERBOSE
    curl_easy_setopt(c, CURLOPT_VERBOSE, 1L);
#endif
    CURLcode res = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdrs); curl_easy_cleanup(c);

    if (res != CURLE_OK) {
        snprintf(r.message,sizeof(r.message),"curl: %s",curl_easy_strerror(res));
        curlbuf_free(buf); return r;
    }
    if (code==200||code==201) {
        char *oid = json_field(buf->data,"id");
        char *fp  = json_field(buf->data,"fill_price");
        char *st  = json_field(buf->data,"status");
        r.ok=1; r.order_id=oid?atol(oid):0;
        r.fill_price = fp?atof(fp):limit;
        snprintf(r.message,sizeof(r.message),
                 "id=%s fill_price=%s status=%s",
                 oid?oid:"?",fp?fp:"pending",st?st:"unknown");
        free(oid);free(fp);free(st);
        order_log_append(action,ticker,qty,r.fill_price,"FILLED",r.order_id,"generic");
    } else {
        char *msg = json_field(buf->data,"message");
        if (!msg) msg = json_field(buf->data,"error");
        snprintf(r.message,sizeof(r.message),"HTTP %ld — %s",
                 code,msg?msg:buf->data);
        free(msg);
        order_log_append(action,ticker,qty,0.0,"REJECTED_API",0,"generic");
    }
    curlbuf_free(buf); return r;
}

/* Generic GET + print */
static int generic_get_print(const char *path, char **env) {
    const char *base = my_getenv("BROKER_API",env);
    if (!base||!base[0]) { fputs("[broker] BROKER_API not set\n",stderr); return 1; }
    char url[512]; snprintf(url,sizeof(url),"%s%s",base,path);
    CURL *c = curl_easy_init(); if (!c) return 1;
    CurlBuf *buf = curlbuf_new(); long code = 0;
    curl_easy_setopt(c, CURLOPT_URL,           url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,     buf);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,       10L);
    curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    if (code==200) { puts(buf->data); curlbuf_free(buf); return 0; }
    fprintf(stderr,"[broker] HTTP %ld — %s\n",code,buf->data);
    curlbuf_free(buf); return 1;
}

static int generic_cancel(const char *oid, char **env) {
    const char *base = my_getenv("BROKER_API",env);
    if (!base||!base[0]) { fputs("[broker] BROKER_API not set\n",stderr); return 1; }
    char url[512]; snprintf(url,sizeof(url),"%s/orders/%s",base,oid);
    CURL *c = curl_easy_init(); if (!c) return 1;
    CurlBuf *buf = curlbuf_new(); long code = 0;
    curl_easy_setopt(c, CURLOPT_URL,           url);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,     buf);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,       10L);
    curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c); curlbuf_free(buf);
    if (code==200||code==204) { printf("[broker] Order %s cancelled.\n",oid); return 0; }
    fprintf(stderr,"[broker] cancel HTTP %ld\n",code); return 1;
}

#endif /* LAS_SHELL_HAVE_CURL */

/* ═══════════════════════════════════════════════════════════════════════
 * §6  Routing helpers
 * ═══════════════════════════════════════════════════════════════════════ */

/*
 * Returns: "paper" | "alpaca" | "ibkr" | "generic"
 *
 * "paper" means in-process ledger (no network).
 * "generic" includes the ACCOUNT=PAPER + BROKER_API=sim_server path.
 */
static const char *detect_broker(char **env) {
    const char *account = my_getenv("ACCOUNT", env);
    if (!account) account = "PAPER";
    if (!strcasecmp(account,"PAPER")) {
        const char *api = my_getenv("BROKER_API",env);
        return (api && api[0]) ? "generic" : "paper";
    }
    const char *broker = my_getenv("BROKER", env);
    if (!broker || !broker[0]) return "generic";
    if (!strcasecmp(broker,"ALPACA")) return "alpaca";
    if (!strcasecmp(broker,"IBKR"))   return "ibkr";
    return "generic";
}

static TimeInForce parse_tif(const char *s) {
    if (!s) return TIF_DAY;
    if (!strcasecmp(s,"gtc")) return TIF_GTC;
    if (!strcasecmp(s,"ioc")) return TIF_IOC;
    if (!strcasecmp(s,"fok")) return TIF_FOK;
    return TIF_DAY;
}

/* ═══════════════════════════════════════════════════════════════════════
 * §7  Public command_* functions (called from main.c dispatch)
 * ═══════════════════════════════════════════════════════════════════════ */

/*
 * order buy|sell TICKER SIZE [market|limit|stop PRICE] [--tif DAY|GTC|IOC|FOK]
 */
int command_order(char **args, char **env) {
    if (!args[1]||!args[2]||!args[3]) {
        fputs("Usage: order buy|sell TICKER SIZE [market|limit PRICE] [--tif DAY|GTC|IOC|FOK]\n"
              "  order buy  SPY  100 market\n"
              "  order sell AAPL  50 limit 185.00\n"
              "  order buy  QQQ  200 limit 430.00 --tif GTC\n", stderr);
        return 1;
    }

    const char *action = args[1];
    if (strcasecmp(action,"buy")&&strcasecmp(action,"sell")) {
        fprintf(stderr,"order: action must be 'buy' or 'sell', got '%s'\n",action);
        return 1;
    }

    char ticker[MAX_TICKER_LEN] = {0};
    strncpy(ticker, args[2], sizeof(ticker)-1);
    ticker_upper(ticker);

    int qty = atoi(args[3]);
    if (qty <= 0) { fputs("order: SIZE must be a positive integer\n",stderr); return 1; }

    OrderType   otype = OTYPE_MARKET;
    double      limit = 0.0;
    TimeInForce tif   = TIF_DAY;

    for (int i = 4; args[i]; i++) {
        if (!strcasecmp(args[i],"--tif") && args[i+1]) {
            tif = parse_tif(args[++i]);
        } else if (!strcasecmp(args[i],"market")) {
            otype = OTYPE_MARKET;
        } else if (!strcasecmp(args[i],"limit")) {
            otype = OTYPE_LIMIT;
            if (args[i+1] && args[i+1][0]!='-') limit = atof(args[++i]);
        } else if (!strcasecmp(args[i],"stop")) {
            otype = OTYPE_STOP;
            if (args[i+1] && args[i+1][0]!='-') limit = atof(args[++i]);
        } else {
            double v = atof(args[i]);
            if (v > 0.0) limit = v;
        }
    }

    if ((otype==OTYPE_LIMIT||otype==OTYPE_STOP) && limit<=0.0) {
        fputs("order: limit/stop requires a PRICE > 0\n",stderr); return 1;
    }

    const char *bn = detect_broker(env);

    /* ── Pre-trade risk check ───────────────────────────────────────── */
    RiskConfig *rc = (RiskConfig *)get_risk_config();
    if (rc && rc->max_position_size > 0 && (double)qty > rc->max_position_size) {
        fprintf(stderr,
            "[risk] REJECTED — qty %d exceeds MAX_POSITION_SIZE %.0f\n",
            qty, rc->max_position_size);
        order_log_append(action,ticker,qty,limit,"REJECTED_RISK_SIZE",0,bn);
        return 1;
    }
    if (rc && rc->has_allowed_list && rc->allowed_count > 0) {
        int ok = 0;
        for (int k=0; k<rc->allowed_count; k++)
            if (!strcmp(rc->allowed_symbols[k],ticker)) { ok=1; break; }
        if (!ok) {
            fprintf(stderr,"[risk] REJECTED — %s not in ALLOWED_SYMBOLS\n",ticker);
            order_log_append(action,ticker,qty,limit,"REJECTED_RISK_SYMBOL",0,bn);
            return 1;
        }
    }

    /* ── Dispatch ───────────────────────────────────────────────────── */
    BrokerResult r = {0};

    if (!strcmp(bn,"paper")) {
        (void)tif; /* tif used only in live adapters below */
        if (!strcasecmp(action,"buy"))
            r = paper_execute_buy (ticker,qty,limit,otype,env);
        else
            r = paper_execute_sell(ticker,qty,limit,otype,env);
    } else {
#ifndef LAS_SHELL_HAVE_CURL
        fputs("[broker] ERROR: live/network orders require libcurl.\n"
              "  Rebuild: make CURL=1\n"
              "  Or: setaccount PAPER  for in-process simulation.\n", stderr);
        return 1;
#else
        if      (!strcmp(bn,"alpaca")) r = alpaca_order (action,ticker,qty,limit,otype,tif,env);
        else if (!strcmp(bn,"ibkr"))   r = ibkr_order   (action,ticker,qty,limit,otype,tif,env);
        else                           r = generic_order (action,ticker,qty,limit,otype,tif,env);
#endif
    }

    /* ── Print result ─────────────────────────────────────────────── */
    if (r.ok) {
        const char *tlabel = (otype==OTYPE_LIMIT)?"LIMIT"
                           : (otype==OTYPE_STOP) ?"STOP"
                           : (otype==OTYPE_STOP_LIMIT)?"STOP_LIMIT":"MARKET";
        printf("[ORDER #%ld]  %-4s %-8s %4d  %s",
               r.order_id,
               strcasecmp(action,"buy")==0?"BUY":"SELL",
               ticker, qty, tlabel);
        if (r.fill_price > 0.0)
            printf(" @ $%.4f  notional=$%.2f", r.fill_price, r.fill_price*qty);
        printf("  [%s]\n", r.message);
        return 0;
    } else {
        fprintf(stderr,"[ORDER REJECTED]  %s %s %d — %s\n",
                action,ticker,qty,r.message);
        return 1;
    }
}

/* positions [--json] [--symbol TICKER] */
int command_positions(char **args, char **env) {
    int json=0; const char *filter=NULL;
    for (int i=1;args[i];i++) {
        if (!strcmp(args[i],"--json"))              json=1;
        else if (!strcmp(args[i],"--symbol")&&args[i+1]) filter=args[++i];
    }
    const char *bn = detect_broker(env);
    if (!strcmp(bn,"paper")) return paper_print_positions(json,filter,env);
#ifndef LAS_SHELL_HAVE_CURL
    fputs("[broker] live positions require libcurl (make CURL=1)\n",stderr); return 1;
#else
    if (!strcmp(bn,"alpaca")) return alpaca_positions(json,filter,env);
    const char *path = !strcmp(bn,"ibkr") ? "/v1/api/portfolio/positions/0" : "/positions";
    return generic_get_print(path,env);
#endif
}

/* balance [--json] */
int command_balance(char **args, char **env) {
    int json=0;
    for (int i=1;args[i];i++) if (!strcmp(args[i],"--json")) json=1;
    const char *bn = detect_broker(env);
    if (!strcmp(bn,"paper")) return paper_print_balance(json,env);
#ifndef LAS_SHELL_HAVE_CURL
    fputs("[broker] live balance requires libcurl (make CURL=1)\n",stderr); return 1;
#else
    if (!strcmp(bn,"alpaca")) return alpaca_balance(json,env);
    const char *path = !strcmp(bn,"ibkr") ? "/v1/api/portfolio/accounts" : "/account";
    return generic_get_print(path,env);
#endif
}

/* cancel ORDER_ID */
int command_cancel(char **args, char **env) {
    if (!args[1]) { fputs("Usage: cancel ORDER_ID\n",stderr); return 1; }
    const char *bn = detect_broker(env);
    if (!strcmp(bn,"paper")) {
        printf("[broker] cancel %s — paper mode fills immediately; nothing to cancel.\n",args[1]);
        return 0;
    }
#ifndef LAS_SHELL_HAVE_CURL
    fputs("[broker] cancel requires libcurl (make CURL=1)\n",stderr); return 1;
#else
    if (!strcmp(bn,"alpaca")) return alpaca_cancel(args[1],env);
    return generic_cancel(args[1],env);
#endif
}

/* close_all — flatten every open position */
int command_close_all(char **args, char **env) {
    (void)args;
    const char *bn = detect_broker(env);

    if (strcmp(bn,"paper")) {
#ifndef LAS_SHELL_HAVE_CURL
        fputs("[broker] close_all (live) requires libcurl (make CURL=1)\n",stderr); return 1;
#else
        fputs("[broker] close_all: fetching live positions to flatten...\n",stderr);
        const char *path = !strcmp(bn,"ibkr")?"/v1/api/portfolio/positions/0":"/positions";
        return generic_get_print(path,env); /* user pipes result through close logic */
#endif
    }

    paper_load();
    int closed = 0;
    for (int i=0; i<g_paper.pos_count; i++) {
        Position *p = &g_paper.positions[i];
        if (!p->quantity) continue;
        char qty_buf[16]; snprintf(qty_buf,sizeof(qty_buf),"%d",p->quantity);
        char *ca[] = {"order","sell",p->ticker,qty_buf,"market",NULL};
        if (command_order(ca,env)==0) closed++;
    }
    printf("[broker] Flattened %d position(s).\n",closed);
    return 0;
}

/* reset_paper [--capital AMOUNT] */
int command_reset_paper(char **args, char **env) {
    (void)env;
    double cap = 100000.0;
    for (int i=1;args[i];i++)
        if (!strcmp(args[i],"--capital")&&args[i+1]) {
            cap = atof(args[++i]);
            if (cap<=0) { fputs("reset_paper: --capital must be > 0\n",stderr); return 1; }
        }
    paper_ensure_path();
    if (g_paper_file[0]) unlink(g_paper_file);
    broker_paper_reset_state();
    g_paper.cash=cap; g_paper.starting_cash=cap; g_paper.loaded=1;
    paper_save();
    /* Format capital with thousands separator AND include raw value for scripts */
    long long cap_int = (long long)cap;
    char formatted[64];
    if (cap_int >= 1000000)
        snprintf(formatted, sizeof(formatted), "%lld,%03lld,%03lld",
                 cap_int/1000000, (cap_int%1000000)/1000, cap_int%1000);
    else if (cap_int >= 1000)
        snprintf(formatted, sizeof(formatted), "%lld,%03lld",
                 cap_int/1000, cap_int%1000);
    else
        snprintf(formatted, sizeof(formatted), "%.2f", cap);
    printf("[broker] Paper account reset. Starting capital: $%s (%.0f)\n", formatted, cap);
    return 0;
}

/* broker_status — show configuration and connectivity */
int command_broker_status(char **args, char **env) {
    (void)args;
    const char *account    = my_getenv("ACCOUNT",    env); if (!account)    account    = "PAPER";
    const char *broker     = my_getenv("BROKER",     env); if (!broker)     broker     = "(not set)";
    const char *broker_api = my_getenv("BROKER_API", env); if (!broker_api) broker_api = "(not set)";
    const char *market     = my_getenv("MARKET",     env); if (!market)     market     = "(not set)";
    const char *capital    = my_getenv("CAPITAL",    env); if (!capital)    capital    = "(not set)";

    const char *adapter = detect_broker(env);
#ifdef LAS_SHELL_HAVE_CURL
    const char *curl_status = "available";
#else
    const char *curl_status = "NOT LINKED  (live orders disabled — rebuild with: make CURL=1)";
#endif
    printf("\n  Las_shell Broker Status\n");
    printf("  %s\n","────────────────────────────────────────────────────────");
    printf("  ACCOUNT:     %s\n", account);
    printf("  BROKER:      %s\n", broker);
    printf("  BROKER_API:  %s\n", broker_api);
    printf("  MARKET:      %s\n", market);
    printf("  CAPITAL:     %s\n", capital);
    printf("  Adapter:     %s\n", adapter);
    printf("  libcurl:     %s\n", curl_status);
    if (!strcmp(adapter,"paper")) {
        paper_load();
        printf("  Paper cash:  $%.2f\n",  g_paper.cash);
        printf("  Open pos:    %d\n",     g_paper.pos_count);
        home_path(g_paper_file,sizeof(g_paper_file),".las_shell_paper_account");
        printf("  Ledger:      %s\n",     g_paper_file);
    }
    home_path(g_order_log,sizeof(g_order_log),".las_shell_order_log");
    printf("  Order log:   %s\n\n", g_order_log);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * §8  Utilities called from other modules
 * ═══════════════════════════════════════════════════════════════════════ */

double broker_get_pnl(char **env) {
    if (strcmp(detect_broker(env),"paper")) return 0.0;
    paper_load();
    double equity = g_paper.cash;
    for (int i=0;i<g_paper.pos_count;i++) {
        Position *p=&g_paper.positions[i];
        if (!p->quantity) continue;
        equity += get_market_price(p->ticker,env)*p->quantity;
    }
    double pnl = equity - g_paper.starting_cash;
    write_pnl_file(pnl);
    return pnl;
}

void broker_paper_reset_state(void) {
    memset(&g_paper,0,sizeof(g_paper));
    g_paper.cash=100000.0; g_paper.starting_cash=100000.0;
    g_paper.order_seq=1000; g_paper.loaded=0;
    g_paper_file[0]='\0'; g_order_log[0]='\0'; g_pnl_file[0]='\0';
}
