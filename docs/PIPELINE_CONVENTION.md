# Las_shell Pipeline Strategy Convention v1.0

## Overview

Every pipeline stage is a self-contained process that:
- Reads a **JSON array** of order candidates from **stdin**
- Writes a **filtered/transformed JSON array** to **stdout**
- Logs diagnostics to **stderr** (never stdout)
- Exits **0** on success, **non-zero** on fatal error

This contract is language-agnostic. Stages written in Python, C, Go, or any
other language compose seamlessly via Las_shell's `execute_pipeline()` in
`pipes.c` — **zero changes required**.

---

## The Contract

### Input (stdin)
```json
[
  {
    "symbol":  "SPY",
    "signal":  0.82,
    "size":    0,
    "price":   510.25,
    "side":    "BUY",
    "meta": {
      "_convention": "1.0",
      "strategy":    "momentum",
      "stage":       "universe",
      "timestamp":   "2026-03-22T09:30:00"
    }
  }
]
```

### Output (stdout)
Same schema — a JSON array. Stages may:
- **Remove** candidates (filter)
- **Modify** fields (enrich, resize)
- **Add** fields to `meta` (annotate)
- **Pass through** unchanged (no-op stage)

### Field Reference

| Field    | Type    | Required | Description                                                        |
|----------|---------|----------|--------------------------------------------------------------------|
| `symbol` | string  | ✔        | Ticker symbol (e.g. `"AAPL"`)                                      |
| `signal` | float   | ✔        | Signal strength [-1.0, 1.0]. Positive = bullish, negative = bearish |
| `size`   | integer | ✔        | Number of shares. 0 = not yet sized                                |
| `price`  | float   | ✔        | Reference price at signal generation                               |
| `side`   | string  | ✔        | "BUY" or "SELL"                                                    |
| `meta`   | object  | ✔        | Strategy metadata. Must always contain _convention and stage       |

### Rules

1. **stdout is sacred** — only valid JSON goes to stdout. All logging goes to stderr.
2. **Empty array is valid** — `[]` means all candidates filtered. Not an error.
3. **Pass unknown fields** — stages must forward fields they don't understand.
4. **Idempotent reads** — stages must not assume ordering of input.
5. **Exit codes matter** — `?>` risk gate uses exit code to accept/reject.
6. **Always set `meta.stage`** — update to your stage name before writing output.
7. **Always set `meta._convention`** — use "1.0" for this spec version.

---

## Stage Catalogue

| Stage             | Role                                            | Type     | Typical filter         |
|-------------------|-------------------------------------------------|----------|------------------------|
| `universe`        | Generates initial candidate list from watchlist | Source   | None (producer)        |
| `momentum_filter` | Keeps signals above threshold, sets signal      | Filter   | |signal| >= threshold   |
| `risk_filter`     | Enforces notional/size/blacklist limits         | Filter   | Risk rules             |
| `size_positions`  | Sets size field based on capital allocation     | Enricher | None (annotator)       |
| `execute`         | Sends orders to broker, emits receipts          | Sink     | None (consumer)        |

---

## Las_shell Usage

### Full pipeline
```bash
python3 pipeline/universe.py \
  | python3 pipeline/momentum_filter.py --threshold 0.2 --topn 5 \
  | python3 pipeline/risk_filter.py \
  | python3 pipeline/size_positions.py --model signal \
  | python3 pipeline/execute.py --mode paper
```

### With Las_shell operators
```bash
# Timestamped audit log of every sized order before execution
python3 pipeline/universe.py \
  | python3 pipeline/momentum_filter.py \
  | python3 pipeline/size_positions.py \
  |> logs/sized_orders.csv \
  | python3 pipeline/execute.py --mode paper

# Risk gate: block entire pipeline if any order fails risk check
python3 pipeline/universe.py \
  | python3 pipeline/momentum_filter.py \
  | python3 pipeline/size_positions.py \
  ?> python3 pipeline/risk_filter.py --gate \
  | python3 pipeline/execute.py --mode paper

# Schedule for market open
@09:30:00 python3 pipeline/universe.py \
  | python3 pipeline/momentum_filter.py \
  | python3 pipeline/risk_filter.py \
  | python3 pipeline/size_positions.py \
  | python3 pipeline/execute.py --mode paper
```

### C + Python hybrid pipeline (same contract, different language)
```bash
./pipeline/universe \
  | python3 pipeline/momentum_filter.py --threshold 0.15 \
  | ./pipeline/risk_filter \
  | python3 pipeline/size_positions.py --model signal \
  | python3 pipeline/execute.py --mode paper
```

---

## Error Handling

| Situation                  | Correct behaviour                              |
|----------------------------|------------------------------------------------|
| Invalid JSON on stdin      | Print error to stderr, exit 1                  |
| Empty input []             | Write [] to stdout, exit 0                     |
| All candidates filtered    | Write [] to stdout, exit 0                     |
| Broker API unreachable     | Print error to stderr, exit 1                  |
| Unknown field in candidate | Forward it unchanged                           |

---

## Starter Templates

The following minimal templates demonstrate correct convention compliance.
Both Python and C versions are provided — they are **fully interchangeable**
in any pipeline. Copy, rename, and extend them.

---

### Stage 1 — universe  (Source)

**Python:**
```python
#!/usr/bin/env python3
"""universe_starter.py — produces the initial candidate list."""
import sys, json
from datetime import datetime

SYMBOLS = ["AAPL", "MSFT", "GOOGL", "SPY", "QQQ"]

def main():
    candidates = [
        {
            "symbol": sym,
            "signal": 0.0,
            "size":   0,
            "price":  100.0,   # replace with real price fetch
            "side":   "BUY",
            "meta": {
                "_convention": "1.0",
                "strategy":    "my_strategy",
                "stage":       "universe",
                "timestamp":   datetime.now().isoformat(),
            }
        }
        for sym in SYMBOLS
    ]
    print(json.dumps(candidates))
    print(f"[universe] {len(candidates)} candidates", file=sys.stderr)

if __name__ == "__main__":
    main()
```

**C:**
```c
/* universe_starter.c — source stage. Compile: gcc -O2 -o universe_starter universe_starter.c */
#include <stdio.h>
#include <time.h>

static const char *SYMBOLS[] = {"AAPL","MSFT","GOOGL","SPY","QQQ", NULL};

int main(void) {
    char ts[32];
    time_t now = time(NULL);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    printf("[\n");
    for (int i = 0; SYMBOLS[i]; i++) {
        if (i > 0) printf(",\n");
        printf(
            "  {\"symbol\":\"%s\",\"signal\":0.0,\"size\":0,\"price\":100.0,"
            "\"side\":\"BUY\","
            "\"meta\":{\"_convention\":\"1.0\",\"strategy\":\"my_strategy\","
            "\"stage\":\"universe\",\"timestamp\":\"%s\"}}",
            SYMBOLS[i], ts);
    }
    printf("\n]\n");
    fprintf(stderr, "[universe/c] 5 candidates\n");
    return 0;
}
```

---

### Stage 2 — momentum_filter  (Filter + Enrich)

**Python:**
```python
#!/usr/bin/env python3
"""momentum_filter_starter.py — sets signal, keeps those above threshold."""
import sys, json

THRESHOLD = 0.2

def compute_signal(symbol):
    """Replace with real momentum calculation."""
    import hashlib, random
    seed = int(hashlib.md5(symbol.encode()).hexdigest(), 16) % 9999
    random.seed(seed)
    return round(random.uniform(-1.0, 1.0), 4)

def main():
    raw = sys.stdin.read().strip()
    if not raw or raw == "[]":
        print("[]"); sys.exit(0)
    candidates = json.loads(raw)
    result = []
    for c in candidates:
        signal = compute_signal(c["symbol"])
        c["signal"] = signal
        c["side"]   = "BUY" if signal > 0 else "SELL"
        c["meta"]["stage"] = "momentum_filter"
        if abs(signal) >= THRESHOLD:
            result.append(c)
    print(json.dumps(result))
    print(f"[momentum_filter] {len(candidates)} in -> {len(result)} passed", file=sys.stderr)

if __name__ == "__main__":
    main()
```

**C:**
```c
/* momentum_filter_starter.c — filter stage.
 * Compile: gcc -O2 -o momentum_filter_starter momentum_filter_starter.c -lm
 * For production: replace toy_signal() with real momentum math and use
 * a JSON library (cJSON, jansson) instead of string scanning. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define THRESHOLD 0.20
#define BUFLEN    65536

static double toy_signal(const char *sym) {
    unsigned h = 5381;
    for (const char *p = sym; *p; p++) h = h * 33 + (unsigned char)*p;
    return round(((double)(h % 2000) / 1000.0 - 1.0) * 1000.0) / 1000.0;
}

int main(void) {
    char *buf = malloc(BUFLEN);
    size_t n = fread(buf, 1, BUFLEN-1, stdin);
    buf[n] = '\0';
    if (n == 0 || strcmp(buf,"[]") == 0) { printf("[]\n"); free(buf); return 0; }

    int first = 1;
    printf("[\n");
    const char *p = buf;
    while ((p = strstr(p, "\"symbol\"")) != NULL) {
        p += 8; while (*p && *p != '"') p++; if (!*p) break; p++;
        char sym[16] = {0}; int i = 0;
        while (*p && *p != '"' && i < 15) sym[i++] = *p++;
        double sig = toy_signal(sym);
        if (fabs(sig) < THRESHOLD) {
            fprintf(stderr, "[momentum_filter/c] FILTERED %s (%.3f)\n", sym, sig);
            continue;
        }
        if (!first) printf(",\n");
        printf("  {\"symbol\":\"%s\",\"signal\":%.4f,\"size\":0,\"price\":100.0,"
               "\"side\":\"%s\","
               "\"meta\":{\"_convention\":\"1.0\",\"stage\":\"momentum_filter\"}}",
               sym, sig, sig > 0 ? "BUY" : "SELL");
        first = 0;
    }
    printf("\n]\n");
    free(buf); return 0;
}
```

---

### Stage 3 — risk_filter  (Filter)

**Python:**
```python
#!/usr/bin/env python3
"""risk_filter_starter.py — enforces pre-trade risk rules.
   Works as pipe stage (filter) or ?> gate (exit code)."""
import sys, json, os

MAX_SIZE = 5000; MAX_NOTIONAL = 500_000.0
BLACKLIST = {"GME","AMC","BBBY"}

def passes(c):
    sym = c.get("symbol","").upper()
    size = int(c.get("size",0)); price = float(c.get("price",0))
    if sym in BLACKLIST:           return False, f"{sym} blacklisted"
    if size > 0 and size > MAX_SIZE: return False, f"size {size} > {MAX_SIZE}"
    if size > 0 and size*price > MAX_NOTIONAL:
        return False, f"notional ${size*price:,.0f} > ${MAX_NOTIONAL:,.0f}"
    return True, "ok"

def main():
    raw = sys.stdin.read().strip()
    if not raw or raw == "[]": print("[]"); sys.exit(0)
    candidates = json.loads(raw)
    result, rejected = [], False
    for c in candidates:
        ok, reason = passes(c)
        if ok:
            c["meta"]["stage"] = "risk_filter"; result.append(c)
        else:
            rejected = True
            print(f"[risk_filter] REJECTED {c.get('symbol')}: {reason}", file=sys.stderr)
            home = os.environ.get("HOME",".")
            with open(f"{home}/.las_shell_risk_rejections","a") as f:
                from datetime import datetime
                f.write(f"{datetime.now().isoformat()},REJECTED,{c.get('symbol')},{reason}\n")
    print(json.dumps(result))
    print(f"[risk_filter] {len(candidates)} in -> {len(result)} passed", file=sys.stderr)
    if "--gate" in sys.argv and rejected: sys.exit(1)

if __name__ == "__main__":
    main()
```

**C:**
```c
/* risk_filter_starter.c — filter/gate stage.
 * Compile: gcc -O2 -o risk_filter_starter risk_filter_starter.c
 * --gate flag: exit 1 if any candidate rejected (for ?> operator). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SIZE 5000
#define BUFLEN   65536

static const char *BLACKLIST[] = {"GME","AMC","BBBY", NULL};

static int blacklisted(const char *s) {
    for (int i = 0; BLACKLIST[i]; i++) if (!strcmp(s,BLACKLIST[i])) return 1;
    return 0;
}

int main(int argc, char **argv) {
    int gate = 0;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i],"--gate")) gate = 1;

    char *buf = malloc(BUFLEN);
    size_t n = fread(buf, 1, BUFLEN-1, stdin); buf[n] = '\0';
    if (n == 0 || !strcmp(buf,"[]")) { printf("[]\n"); free(buf); return 0; }

    int rejected = 0, first = 1;
    printf("[\n");
    const char *p = buf;
    while ((p = strstr(p,"\"symbol\"")) != NULL) {
        p += 8; while (*p && *p != '"') p++; if (!*p) break; p++;
        char sym[16] = {0}; int i = 0;
        while (*p && *p != '"' && i < 15) sym[i++] = *p++;
        if (blacklisted(sym)) {
            fprintf(stderr,"[risk_filter/c] REJECTED %s: blacklisted\n",sym);
            rejected = 1; continue;
        }
        if (!first) printf(",\n");
        printf("  {\"symbol\":\"%s\",\"signal\":0.0,\"size\":0,\"price\":100.0,"
               "\"side\":\"BUY\","
               "\"meta\":{\"_convention\":\"1.0\",\"stage\":\"risk_filter\","
               "\"risk_status\":\"PASSED\"}}", sym);
        first = 0;
    }
    printf("\n]\n");
    free(buf);
    return (gate && rejected) ? 1 : 0;
}
```

---

### Stage 4 — size_positions  (Enricher)

**Python:**
```python
#!/usr/bin/env python3
"""size_positions_starter.py — sets size field. Never filters."""
import sys, json, os

def main():
    capital = float(os.environ.get("CAPITAL", 100_000))
    raw = sys.stdin.read().strip()
    if not raw or raw == "[]": print("[]"); sys.exit(0)
    candidates = json.loads(raw)
    n = len(candidates)
    if n == 0: print("[]"); sys.exit(0)
    weight = 1.0 / n   # equal weight — replace with your model
    for c in candidates:
        price = float(c.get("price", 1.0))
        c["size"] = max(1, int(capital * weight / price))
        c["meta"]["stage"]   = "size_positions"
        c["meta"]["capital"] = capital
        c["meta"]["weight"]  = round(weight, 4)
    print(json.dumps(candidates))
    print(f"[size_positions] {n} sized | capital=${capital:,.0f}", file=sys.stderr)

if __name__ == "__main__":
    main()
```

**C:**
```c
/* size_positions_starter.c — enricher stage. Sets size field. Never filters.
 * Reads CAPITAL from environment. Default: 100000.
 * Compile: gcc -O2 -o size_positions_starter size_positions_starter.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFLEN 65536

int main(void) {
    char *cap_env = getenv("CAPITAL");
    double capital = cap_env ? atof(cap_env) : 100000.0;

    char *buf = malloc(BUFLEN);
    size_t n = fread(buf, 1, BUFLEN-1, stdin); buf[n] = '\0';
    if (n == 0 || !strcmp(buf,"[]")) { printf("[]\n"); free(buf); return 0; }

    /* Count symbols */
    int count = 0; const char *q = buf;
    while ((q = strstr(q,"\"symbol\"")) != NULL) { count++; q += 8; }
    if (count == 0) { printf("[]\n"); free(buf); return 0; }

    double weight = 1.0 / count;
    int first = 1;
    printf("[\n");
    const char *p = buf;
    while ((p = strstr(p,"\"symbol\"")) != NULL) {
        p += 8; while (*p && *p != '"') p++; if (!*p) break; p++;
        char sym[16] = {0}; int i = 0;
        while (*p && *p != '"' && i < 15) sym[i++] = *p++;
        double price = 100.0;   /* replace with parsed price */
        int size = (int)(capital * weight / price);
        if (size < 1) size = 1;
        if (!first) printf(",\n");
        printf("  {\"symbol\":\"%s\",\"signal\":0.0,\"size\":%d,\"price\":%.2f,"
               "\"side\":\"BUY\","
               "\"meta\":{\"_convention\":\"1.0\",\"stage\":\"size_positions\","
               "\"capital\":%.2f,\"weight\":%.4f}}", sym, size, price, capital, weight);
        first = 0;
    }
    printf("\n]\n");
    fprintf(stderr,"[size_positions/c] %d sized | capital=%.0f\n",count,capital);
    free(buf); return 0;
}
```

---

### Stage 5 — execute  (Sink)

**Python:**
```python
#!/usr/bin/env python3
"""execute_starter.py — sends orders to broker. Emits receipts for |> logging."""
import sys, json, os
from datetime import datetime

def main():
    mode = os.environ.get("ACCOUNT","paper").lower()
    if mode == "live" and not os.environ.get("LAS_SHELL_LIVE_CONFIRMED"):
        print("[execute] LIVE mode requires LAS_SHELL_LIVE_CONFIRMED=1", file=sys.stderr)
        sys.exit(1)

    raw = sys.stdin.read().strip()
    if not raw or raw == "[]":
        print("[]"); print("[execute] no orders", file=sys.stderr); sys.exit(0)

    candidates = json.loads(raw)
    receipts, ok = [], True

    for c in candidates:
        if c.get("size",0) == 0:
            print(f"[execute] {c['symbol']} unsized", file=sys.stderr); ok=False; continue
        receipt = {
            "symbol":     c["symbol"], "side":       c["side"],
            "size":       c["size"],   "fill_price": c["price"],
            "notional":   round(c["size"] * c["price"], 2),
            "mode":       mode,        "status":     "FILLED",
            "timestamp":  datetime.now().isoformat(),
            "meta":       {**c.get("meta",{}), "stage": "execute"},
        }
        receipts.append(receipt)
        print(f"[execute] {mode.upper()} {receipt['side']} {receipt['size']} "
              f"{receipt['symbol']} @ ${receipt['fill_price']:.2f}", file=sys.stderr)

    print(json.dumps(receipts))
    total = sum(r["notional"] for r in receipts)
    print(f"[execute] {len(receipts)} orders | total=${total:,.0f}", file=sys.stderr)
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
```

**C:**
```c
/* execute_starter.c — sink stage. Emits execution receipts to stdout.
 * Receipts are compatible with |> CSV logging.
 * Compile: gcc -O2 -o execute_starter execute_starter.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BUFLEN 65536

int main(void) {
    char *account = getenv("ACCOUNT");
    int  live = account && !strcmp(account,"LIVE");
    if (live && !getenv("LAS_SHELL_LIVE_CONFIRMED")) {
        fprintf(stderr,"[execute/c] LIVE mode requires LAS_SHELL_LIVE_CONFIRMED=1\n");
        return 1;
    }
    const char *mode = live ? "live" : "paper";

    char *buf = malloc(BUFLEN);
    size_t n = fread(buf,1,BUFLEN-1,stdin); buf[n] = '\0';
    if (n == 0 || !strcmp(buf,"[]")) {
        printf("[]\n"); fprintf(stderr,"[execute/c] no orders\n"); free(buf); return 0;
    }

    char ts[32]; time_t now = time(NULL);
    strftime(ts,sizeof(ts),"%Y-%m-%dT%H:%M:%S",localtime(&now));

    int first = 1, count = 0;
    printf("[\n");
    const char *p = buf;
    while ((p = strstr(p,"\"symbol\"")) != NULL) {
        p += 8; while (*p && *p != '"') p++; if (!*p) break; p++;
        char sym[16]={0}; int i=0;
        while (*p && *p != '"' && i<15) sym[i++] = *p++;
        double price=100.0; int size=100; /* replace with parsed values */
        if (!first) printf(",\n");
        printf("  {\"symbol\":\"%s\",\"side\":\"BUY\",\"size\":%d,"
               "\"fill_price\":%.2f,\"notional\":%.2f,"
               "\"mode\":\"%s\",\"status\":\"FILLED\",\"timestamp\":\"%s\","
               "\"meta\":{\"_convention\":\"1.0\",\"stage\":\"execute\"}}",
               sym,size,price,price*size,mode,ts);
        first=0; count++;
        fprintf(stderr,"[execute/c] %s BUY %d %s @ $%.2f\n",mode,size,sym,price);
    }
    printf("\n]\n");
    fprintf(stderr,"[execute/c] %d orders | mode=%s\n",count,mode);
    free(buf); return 0;
}
```

---

## Adding a Custom Stage

Four-step pattern for any language:

```
1. Read all of stdin into a string
2. Parse as JSON array — exit 1 on parse error
3. Apply your logic (filter, enrich, or transform each candidate)
4. Dump the result array as JSON to stdout — exit 0
```

The stage composes with any other stage in any pipeline immediately,
with no changes to Las_shell or pipes.c.

---

## Versioning

Add `_convention` to `meta` on every candidate:

```json
{
  "symbol": "AAPL", "signal": 0.8, "size": 0, "price": 185.0, "side": "BUY",
  "meta": {"_convention": "1.0", "strategy": "momentum", "stage": "universe"}
}
```

Current version: **0.5.0**. Future versions remain backward-compatible —
new fields are always optional, and stages must forward unknown fields unchanged.
