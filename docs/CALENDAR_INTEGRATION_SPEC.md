# Las_shell Integration Specification

**Version:** 1.0.0  
**Target Las_shell:** v0.6.0 Milestone 3  
**Source registry:** exchange-calendar v1.0.0

---

## Table of Contents

- [Overview](#overview)
- [Current State Analysis](#current-state-analysis)
- [Target Architecture](#target-architecture)
- [C API](#c-api)
- [File Paths](#file-paths)
- [Component Integration](#component-integration)
- [Deletion of market_daemon.sh](#deletion-of-market_daemonsh)
- [New Operators](#new-operators)
- [Error Handling](#error-handling)
- [Testing Strategy](#testing-strategy)
- [Migration Plan](#migration-plan)

---

## Overview

This document specifies how Las_shell v0.6.0 consumes the exchange-calendar
registry to replace the hardcoded `market_daemon.sh` with a proper calendar
engine.

**The goal:** Las_shell knows exactly when any of 14 exchanges is open,
closed, in lunch break, or closing early — including holidays, observed
days, and timezone differences.

**The result:** The shell's market status is truthful on every day, not
approximately correct on most days.

---

## Current State Analysis

### What exists in v0.5.5

| Component | Behavior | Problems |
|-----------|----------|----------|
| `market_daemon.sh` | Bash loop, 5-second polling | No holidays, no early closes, NYSE only |
| `~/.las_shell_market` | Status file written by daemon | Format: `STATUS MINUTES` |
| `src/prompt.c` | Reads status file | Depends on daemon running |
| `@time` operator | Wall-clock blocking | No trading-day awareness |
| `MARKET` env var | Cosmetic (prompt badge) | Doesn't affect market monitoring |
| `order` command | No market-hours check | Can place orders on weekends |

### Known bugs from v0.5.5 review

1. **Sunday countdown formula is wrong** — double-counts Monday
2. **No holiday detection** — prompt shows "OPEN" on MLK Day
3. **No early close awareness** — prompt shows "OPEN" on Black Friday at 14:00
4. **Only supports NYSE** — `MARKET=LSE` shows LSE badge but NYSE hours
5. **`date +%u` DST handling fragile** — edge cases around DST transitions

---

## Target Architecture

```
Las_shell startup
    │
    ├── load_config()           # Read ~/.trading_env → MARKET, ACCOUNT, etc.
    │
    ├── calendar_init()         # Load calendar.json from install path
    │   ├── Success → use registry
    │   └── Failure → built-in NYSE fallback with warning
    │
    └── main_loop()
        │
        ├── update_market_badge()
        │   ├── calendar_status(MARKET)      # "open", "closed", "pre", etc.
        │   ├── calendar_minutes_until_change(MARKET)
        │   └── render badge                  # Same format as v0.5.5
        │
        ├── check_scheduled_commands()
        │   ├── @HH:MM:SS                     # Wall-clock (unchanged)
        │   ├── @next_open                    # NEW: calendar-aware
        │   └── @before_close N               # NEW: calendar-aware
        │
        └── order_gate() (optional)
            └── calendar_is_open(MARKET)     # Block orders when market closed
```

---

## C API

### New header: `include/calendar.h`

```c
#ifndef LAS_SHELL_CALENDAR_H
#define LAS_SHELL_CALENDAR_H

#include <time.h>

/* ──────────────────────────────────────────────
 * Initialization and lifecycle
 * ────────────────────────────────────────────── */

/**
 * Load the exchange calendar registry.
 * Called once at startup. Returns 0 on success.
 * On failure, Las_shell falls back to built-in NYSE defaults.
 */
int calendar_init(const char *path);

/**
 * Free all calendar resources at shutdown.
 */
void calendar_cleanup(void);

/* ──────────────────────────────────────────────
 * Status queries
 * ────────────────────────────────────────────── */

/**
 * Return the current session status for an exchange.
 * Values: "closed", "pre", "open", "lunch_break", "after", "early_close"
 * Returns "closed" on error or unknown exchange.
 */
const char* calendar_status(const char *exchange);

/**
 * Return minutes until the next state change.
 * Returns -1 on error.
 */
int calendar_minutes_until_change(const char *exchange);

/**
 * Return 1 if the exchange is currently open for trading
 * (regular hours or early close before close time).
 * Return 0 otherwise.
 */
int calendar_is_open(const char *exchange);

/* ──────────────────────────────────────────────
 * Date-specific queries
 * ────────────────────────────────────────────── */

/**
 * Return 1 if the given date is a full market closure
 * (weekend or holiday) for the exchange.
 * date_iso: "YYYY-MM-DD"
 */
int calendar_is_holiday(const char *exchange, const char *date_iso);

/**
 * Return 1 if the given date has an early close.
 */
int calendar_is_early_close(const char *exchange, const char *date_iso);

/**
 * Return the early close time as "HH:MM" if the date is an early close.
 * Return NULL otherwise.
 */
const char* calendar_early_close_time(const char *exchange, const char *date_iso);

/* ──────────────────────────────────────────────
 * Scheduling support
 * ────────────────────────────────────────────── */

/**
 * Return the Unix timestamp of the next market open for the exchange.
 * Returns 0 on error.
 */
time_t calendar_next_open_time(const char *exchange);

/**
 * Return the Unix timestamp of the next market close for the exchange.
 * Returns 0 on error.
 */
time_t calendar_next_close_time(const char *exchange);

/* ──────────────────────────────────────────────
 * Utility
 * ────────────────────────────────────────────── */

/**
 * List all exchanges in the loaded registry to stderr.
 * Used for the `calendar list` built-in command.
 */
void calendar_list_exchanges(void);

#endif /* LAS_SHELL_CALENDAR_H */
```

### Status value mapping

The C API status values align with the exchange-calendar registry's `SessionStatus`:

| C API Value | Registry Value | Description |
|-------------|---------------|-------------|
| `"closed"` | `"closed"` | Outside market hours, weekend, or holiday |
| `"pre"` | `"pre_market"` | Pre-market session |
| `"open"` | `"open"` | Regular trading hours |
| `"lunch_break"` | `"lunch_break"` | Midday break (XTKS, XHKG, XSHG) |
| `"after"` | `"after_hours"` | After-hours session |
| `"early_close"` | `"early_close"` | Open but closing early today |

**Note:** The C API uses shorter names (`"pre"`, `"after"`) to match the
existing `market_daemon.sh` output format. The registry uses the full
names (`"pre_market"`, `"after_hours"`). The integration layer maps between them.

---

## File Paths

### Registry file location

| Priority | Path | Condition |
|----------|------|-----------|
| 1 | `$LAS_SHELL_HOME/calendar.json` | If `LAS_SHELL_HOME` is set |
| 2 | `/usr/local/share/las_shell/calendar.json` | Default install path |
| 3 | Built-in NYSE fallback | No calendar file found |

### Install target

```makefile
# In Las_shell Makefile
install:
    # ... existing targets ...
    install -m 644 calendar.json $(DESTDIR)/usr/local/share/las_shell/calendar.json
```

### Runtime lookup order in `calendar_init()`

```c
int calendar_init(const char *path) {
    // 1. Explicit path if provided
    if (path && access(path, R_OK) == 0) {
        return calendar_load_from_file(path);
    }

    // 2. LAS_SHELL_HOME
    const char *home = getenv("LAS_SHELL_HOME");
    if (home) {
        char buf[PATH_MAX];
        snprintf(buf, sizeof(buf), "%s/calendar.json", home);
        if (access(buf, R_OK) == 0) {
            return calendar_load_from_file(buf);
        }
    }

    // 3. Default install path
    if (access("/usr/local/share/las_shell/calendar.json", R_OK) == 0) {
        return calendar_load_from_file("/usr/local/share/las_shell/calendar.json");
    }

    // 4. Built-in fallback
    fprintf(stderr, "[WARN] calendar.json not found, using built-in NYSE defaults\n");
    return calendar_init_builtin_nyse();
}
```

---

## Component Integration

### 1. Prompt (`src/prompt.c`)

**Current:** Reads `~/.las_shell_market` file.

**Target:** Calls `calendar_status()` and `calendar_minutes_until_change()` directly.

```c
void build_market_badge(char *buf, size_t size) {
    const char *exchange = getenv("MARKET") ? getenv("MARKET") : "NYSE";
    const char *status = calendar_status(exchange);
    int minutes = calendar_minutes_until_change(exchange);

    // Same output format as v0.5.5
    snprintf(buf, size, "[%s: %s %+05d]", exchange, uppercase(status), minutes);
}
```

**Badge colors remain unchanged:**

| Status | Color |
|--------|-------|
| OPEN | GREEN (positive P&L) / YELLOW (negative P&L) |
| PRE | ORANGE |
| CLOSED / AFTER | RED |
| LUNCH_BREAK | ORANGE |
| EARLY_CLOSE | YELLOW |

### 2. `@time` operator (`src/operators.c`)

**Current:** `@HH:MM:SS cmd` blocks until wall-clock time.

**Target:** `@time` remains wall-clock only for backward compatibility.
New operators handle calendar-aware scheduling.

### 3. New `@next_open` operator

```
@next_open NYSE strategy.sh     # Run at next NYSE open
@next_open strategy.sh          # Use $MARKET env var
```

**Implementation:**

```c
int operator_next_open(char *command) {
    const char *exchange = getenv("MARKET") ? getenv("MARKET") : "NYSE";

    // Parse optional exchange argument
    // ...

    time_t open_time = calendar_next_open_time(exchange);
    if (open_time == 0) {
        fprintf(stderr, "Error: no future open time for %s\n", exchange);
        return 1;
    }

    // Block until open_time
    return wait_until_timestamp(open_time, command);
}
```

### 4. New `@before_close` operator

```
@before_close NYSE 30 flatten     # 30 minutes before NYSE close
@before_close 30 flatten          # Use $MARKET, 30 minutes before
```

**Implementation:**

```c
int operator_before_close(char *command) {
    const char *exchange = getenv("MARKET") ? getenv("MARKET") : "NYSE";
    int minutes = atoi(/* parse minutes argument */);

    time_t close_time = calendar_next_close_time(exchange);
    if (close_time == 0) {
        fprintf(stderr, "Error: no future close time for %s\n", exchange);
        return 1;
    }

    time_t fire_time = close_time - (minutes * 60);
    return wait_until_timestamp(fire_time, command);
}
```

### 5. `order` command (`src/Commands.c`)

**Optional market-hours gate** (enabled via `~/.las_shell_risk`):

```c
// In risk_config.c: add new option
// ORDER_MARKET_HOURS_GATE = true
```

When enabled:

```c
int command_order(char **args) {
    // ... existing validation ...

    if (risk_config.order_market_hours_gate) {
        const char *exchange = getenv("MARKET") ? getenv("MARKET") : "NYSE";
        if (!calendar_is_open(exchange)) {
            fprintf(stderr, "Error: %s is currently closed. Order rejected.\n", exchange);
            return 1;
        }
    }

    // ... existing order logic ...
}
```

---

## Deletion of market_daemon.sh

### What gets deleted

- `scripts/market_daemon.sh` — the entire bash script
- `scripts/quote.sh` — wrapper if it references market_daemon.sh

### What replaces it

The calendar engine runs **in-process** within Las_shell. No background
daemon, no polling, no status file.

### Migration of `~/.las_shell_market`

The file is no longer written. If it exists from a previous version,
Las_shell ignores it. Users can delete it:

```bash
rm ~/.las_shell_market
```

### Backward compatibility

If `calendar_init()` fails (no calendar.json found), Las_shell falls back
to built-in NYSE defaults with the same output format. The prompt badge
continues to work, though without holiday awareness.

---

## Error Handling

| Scenario | Behavior |
|----------|----------|
| No calendar.json | Fallback to built-in NYSE defaults + stderr warning |
| Invalid calendar.json | Fallback to built-in NYSE defaults + stderr error |
| Unknown exchange code | `calendar_status()` returns `"closed"` |
| Date format error | `calendar_is_holiday()` returns 0 (not holiday) |
| No future open time | `@next_open` errors: "No future open time" |
| No future close time | `@before_close` errors: "No future close time" |

---

## Testing Strategy

### Unit tests (C)

| Test | What it verifies |
|------|-----------------|
| `test_calendar_init` | Loads valid calendar.json |
| `test_calendar_init_missing` | Falls back to built-in NYSE |
| `test_calendar_init_invalid` | Handles malformed JSON |
| `test_calendar_status_open` | Returns "open" on Tuesday 10:00 NYSE |
| `test_calendar_status_holiday` | Returns "closed" on MLK Day |
| `test_calendar_status_early_close` | Returns "early_close" before 13:00 |
| `test_calendar_status_lunch_break` | Returns "lunch_break" on XTKS at 12:00 |
| `test_calendar_minutes_until_change` | Correct countdown at boundaries |
| `test_calendar_next_open_time` | Returns Monday 09:30 on Friday |
| `test_calendar_next_close_time` | Returns Friday 16:00 on Thursday |
| `test_calendar_is_holiday` | True for holidays, false for weekdays |
| `test_calendar_early_close_time` | Returns "13:00" for Black Friday |

### Integration tests (shell level)

| Test | What it verifies |
|------|-----------------|
| `@next_open NYSE cmd` | Fires at next market open |
| `@before_close NYSE 30 cmd` | Fires 30 min before close |
| Prompt badge on MLK Day | Shows "CLOSED" |
| Prompt badge on Black Friday | Shows "EARLY_CLOSE 13:00" |
| `MARKET=XTKS` prompt | Shows TSE hours with lunch break |
| `order` with gate enabled | Rejects orders when market closed |

### Cross-exchange tests

| Test | What it verifies |
|------|-----------------|
| `MARKET=XLON` | LSE hours (08:00-16:30), 12:30 early closes |
| `MARKET=XTKS` | Lunch break 11:30-12:30, close 15:30 |
| `MARKET=XASX` | Sydney timezone, 14:10 half-days |
| `MARKET=XSES` | Continuous trading, CNY Eve half-days |

---

## Migration Plan

### Phase 1: Calendar library (Week 1)

1. Implement `src/calendar.c` and `include/calendar.h`
2. Write unit tests (12 tests)
3. Load calendar.json, parse, index by date
4. Implement all status queries
5. Verify against known dates from exchange-calendar tests

### Phase 2: Prompt integration (Week 1-2)

1. Modify `src/prompt.c` to call calendar API
2. Remove `read_home_file()` dependency for market status
3. Keep the same output format
4. Test prompt badge on regular days, holidays, early closes

### Phase 3: Scheduling operators (Week 2-3)

1. Implement `@next_open` operator
2. Implement `@before_close` operator
3. Add to `operators.c`
4. Test with all 14 exchanges

### Phase 4: Order gate (Week 3)

1. Add `ORDER_MARKET_HOURS_GATE` to `~/.las_shell_risk`
2. Modify `command_order()` to check gate
3. Test paper orders on weekends, holidays, early closes

### Phase 5: Cleanup (Week 3-4)

1. Delete `scripts/market_daemon.sh`
2. Update Makefile to install calendar.json
3. Remove references to `~/.las_shell_market`
4. Update documentation
5. Full test suite

### Phase 6: Multi-exchange (Week 4+)

1. `calendar list` built-in command
2. Multi-exchange prompt badge
3. `MARKET` env var fully functional
4. Per-exchange scheduling

---

## Summary

| Component | Current (v0.5.5) | Target (v0.6.0) |
|-----------|------------------|-----------------|
| Market status | `market_daemon.sh` bash loop | C `calendar_status()` |
| Holiday detection | None | Full, from registry |
| Early close detection | None | Full, from registry |
| Lunch break detection | None | Full, from registry |
| Multi-exchange | NYSE only | 14 exchanges |
| `@time` operator | Wall-clock only | Unchanged (backward compat) |
| `@next_open` | Doesn't exist | Calendar-aware scheduling |
| `@before_close` | Doesn't exist | Calendar-aware scheduling |
| `order` gate | None | Optional, via risk config |
| Status file | `~/.las_shell_market` | Deleted (in-process) |
| Prompt badge | Reads file | Calls API directly |

---

## See Also

- [exchange_schema.md](exchange_schema.md) — Data format
- [recurrence_rules.md](recurrence_rules.md) — Date generation
- [contributing_exchange.md](contributing_exchange.md) — Adding exchanges
- [Las_shell Repository](https://github.com/slimissa/Las_shell)

---