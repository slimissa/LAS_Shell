# QShell Pipeline Strategy Convention

## Overview

Every pipeline stage is a self-contained process that:
- Reads a **JSON array** of order candidates from **stdin**
- Writes a **filtered/transformed JSON array** to **stdout**
- Logs diagnostics to **stderr** (never stdout)
- Exits **0** on success, **non-zero** on fatal error

This contract is language-agnostic. Stages written in Python, C, Go, or any
other language compose seamlessly via QShell's `execute_pipeline()` in
`pipes.c` — **zero changes required**.

---

## The Contract

### Input (stdin)
```json
[
  {
    "symbol":    "SPY",
    "signal":    0.82,
    "size":      0,
    "price":     510.25,
    "side":      "BUY",
    "meta": {
      "strategy":  "momentum",
      "lookback":  20,
      "timestamp": "2026-03-22T09:30:00"
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

| Field    | Type    | Required | Description                              |
|----------|---------|----------|------------------------------------------|
| `symbol` | string  | ✔        | Ticker symbol (e.g. `"AAPL"`)            |
| `signal` | float   | ✔        | Signal strength `[-1.0, 1.0]`. Positive = bullish, negative = bearish |
| `size`   | integer | ✔        | Number of shares. `0` = not yet sized    |
| `price`  | float   | ✔        | Reference price at signal generation     |
| `side`   | string  | ✔        | `"BUY"` or `"SELL"`                      |
| `meta`   | object  | ✔        | Arbitrary strategy metadata. Never empty |

### Rules

1. **stdout is sacred** — only valid JSON goes to stdout. All logging → stderr.
2. **Empty array is valid** — `[]` means all candidates filtered. Not an error.
3. **Pass unknown fields** — stages must forward fields they don't understand.
4. **Idempotent reads** — stages must not assume ordering of input.
5. **Exit codes matter** — `?>` risk gate uses exit code to accept/reject.

---

## Stage Catalogue

| Stage              | Role                                          | Typical filter |
|--------------------|-----------------------------------------------|---------------|
| `universe`         | Generates initial candidate list from watchlist | None (source) |
| `momentum_filter`  | Keeps signals above threshold                  | `signal >= threshold` |
| `risk_filter`      | Enforces notional/size/blacklist limits        | Risk rules |
| `size_positions`   | Sets `size` field based on capital allocation  | None (enricher) |
| `execute`          | Sends orders to broker API                     | None (sink) |

---

## QShell Usage

### Full pipeline
```bash
python3 universe.py | python3 momentum_filter.py | python3 risk_filter.py | python3 size_positions.py | python3 execute.py
```

### With QShell operators
```bash
# Log every sized order before execution
python3 universe.py | python3 momentum_filter.py | python3 size_positions.py |> logs/sized_orders.csv | python3 execute.py

# Risk gate the entire pipeline output
python3 universe.py | python3 momentum_filter.py | python3 size_positions.py ?> python3 risk_filter.py && python3 execute.py

# Schedule for market open
@09:30:00 python3 universe.py | python3 momentum_filter.py | python3 risk_filter.py | python3 size_positions.py | python3 execute.py
```

### C stages in the same pipeline
```bash
./universe | python3 momentum_filter.py | ./risk_filter | python3 size_positions.py | ./execute
```

---

## Error Handling

| Situation                  | Correct behaviour                            |
|----------------------------|----------------------------------------------|
| Invalid JSON on stdin      | Print error to stderr, exit 1                |
| Empty input `[]`           | Write `[]` to stdout, exit 0                 |
| All candidates filtered    | Write `[]` to stdout, exit 0                 |
| Broker API unreachable     | Print error to stderr, exit 1                |
| Unknown field in candidate | Forward it unchanged                         |

---

## Minimal Stage Template (any language)

```
read JSON array from stdin
for each candidate:
    apply your logic
    if candidate passes: include in output array
write output array as JSON to stdout
exit 0
```

---

## Versioning

Add a `_convention` field to meta when needed:
```json
{"symbol": "AAPL", "signal": 0.8, "size": 0, "price": 185.0, "side": "BUY",
 "meta": {"_convention": "1.0", "strategy": "momentum"}}
```