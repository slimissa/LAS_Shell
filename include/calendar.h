#ifndef LAS_SHELL_CALENDAR_H
#define LAS_SHELL_CALENDAR_H

#include <time.h>

/* ============================================================
 * Las_shell v0.6.0 Milestone 3 — Exchange Calendar Library
 * include/calendar.h
 *
 * Wraps the exchange-calendar registry (github.com/slimissa/
 * exchange-calendar). The registry ships one JSON file per exchange
 * (exchanges/XNYS.json, ...); calendar_init() loads a single bundled
 * calendar.json (built via that repo's tools/build.py) containing
 * every exchange's regular_hours, sessions (e.g. lunch breaks),
 * and pre-expanded holiday dates for a fixed generation_range.
 *
 * This API mirrors docs/CALENDAR_INTEGRATION_SPEC.md (pulled directly
 * from the registry repo, not re-derived) function-for-function. That
 * spec's own count of "12 functions" in the v0.6.0 roadmap does not
 * match either the spec document or this header -- both list 11.
 * Treated as a stale count, not a missing function; not invented one
 * to make the number match.
 *
 * Exchange identifiers: the registry is keyed by ISO 10383 MIC code
 * (XNYS, XLON, XTKS, ...). A small, explicitly-documented alias table
 * (see calendar.c) maps a handful of unambiguous common names (NYSE,
 * NASDAQ, LSE, Tokyo, ...) to their MIC code, matching the existing
 * $MARKET env var convention (default "NYSE") this shell already
 * used before this milestone. Anything not in the small alias table
 * must be given as the exact MIC code.
 *
 * Timezone handling: exchange local time is computed via the
 * standard POSIX TZ-env-var + tzset()/localtime_r() mechanism (no
 * reentrant zoneinfo functions -- tzalloc/localtime_rz are not
 * available in this glibc), serialized behind an internal mutex so
 * two threads (e.g. the checkpoint thread) can't observe each
 * other's TZ mutation mid-computation. The critical section is kept
 * to "set TZ, read local time fields, restore TZ" -- microseconds,
 * not a lock held across I/O.
 * ============================================================ */

/* ── Lifecycle ─────────────────────────────────────────────── */

/*
 * calendar_init(path)
 *   Load the exchange registry. path may be NULL -- in that case
 *   $LAS_SHELL_HOME/calendar.json, then
 *   /usr/local/share/las_shell/calendar.json are tried. If neither
 *   is found, falls back to a built-in NYSE-only definition (regular
 *   hours only, no holiday data) with a stderr warning -- this is a
 *   degraded but successful init, matching currency_init()'s
 *   fallback philosophy: the shell should never fail to start over
 *   a missing data file.
 *   Always returns 0.
 */
int calendar_init(const char *path);

/*
 * calendar_cleanup()
 *   Free all resources. Safe to call even if calendar_init() was
 *   never called.
 */
void calendar_cleanup(void);

/* ── Status queries ───────────────────────────────────────────
 * `exchange` accepts either a MIC code (XNYS) or a small alias
 * (NYSE) -- see calendar.c's alias table. Unknown exchange names
 * behave the same as "no data": calendar_status() returns "closed",
 * calendar_minutes_until_change() returns -1, calendar_is_open()
 * returns 0.
 * ────────────────────────────────────────────────────────────── */

/*
 * calendar_status(exchange)
 *   One of: "closed", "pre", "open", "lunch_break", "after",
 *   "early_close". Returns "closed" on error or unknown exchange.
 *   Returned pointer is to a static buffer -- copy it if it must
 *   outlive the next call on any thread.
 */
const char* calendar_status(const char *exchange);

/*
 * calendar_exchange_exists(exchange)
 *   1 if `exchange` resolves to a loaded exchange (via MIC code or
 *   the alias table), 0 otherwise. Used by callers (e.g. the
 *   @next_open / @before_close operators in src/operators.c) that
 *   need to positively distinguish "this token names an exchange"
 *   from "this token is something else" -- calendar_status()'s
 *   fail-safe "closed" default can't be used for that, since it
 *   looks the same for both an unknown exchange and a real exchange
 *   that is genuinely closed right now.
 */
int calendar_exchange_exists(const char *exchange);

/*
 * calendar_minutes_until_change(exchange)
 *   Minutes until the next session-status change (e.g. minutes left
 *   in the open session, or minutes until pre-market starts).
 *   Returns -1 on error or unknown exchange.
 */
int calendar_minutes_until_change(const char *exchange);

/*
 * calendar_is_open(exchange)
 *   1 if currently open for trading (regular hours, or early-close
 *   day before its early close time). 0 otherwise, including
 *   pre-market/after-hours/lunch-break -- those are extended-session
 *   states, not "open" for this purpose, matching the spec's
 *   "regular hours or early close before close time" definition.
 */
int calendar_is_open(const char *exchange);

/* ── Date-specific queries ─────────────────────────────────────
 * date_iso must be "YYYY-MM-DD". Malformed dates behave like an
 * unknown exchange (safe default: not a holiday, not early-close).
 * ────────────────────────────────────────────────────────────── */

/*
 * calendar_is_holiday(exchange, date_iso)
 *   1 if the date is a full closure for the exchange -- weekend
 *   (derived from the calendar date, not registry data) OR a
 *   registry holiday entry with status "closed". 0 otherwise.
 */
int calendar_is_holiday(const char *exchange, const char *date_iso);

/*
 * calendar_is_early_close(exchange, date_iso)
 *   1 if the date has a registry holiday entry with status
 *   "early_close". 0 otherwise (including weekends/full holidays --
 *   those are calendar_is_holiday(), not this).
 */
int calendar_is_early_close(const char *exchange, const char *date_iso);

/*
 * calendar_early_close_time(exchange, date_iso)
 *   "HH:MM" (exchange local time) if the date is an early close,
 *   NULL otherwise. Returned pointer is to a static buffer.
 */
const char* calendar_early_close_time(const char *exchange, const char *date_iso);

/* ── Scheduling support ────────────────────────────────────────
 * Used by @next_open / @before_close (src/operators.c).
 * ────────────────────────────────────────────────────────────── */

/*
 * calendar_next_open_time(exchange)
 *   Unix timestamp (UTC) of the next regular-hours market open at or
 *   after now, skipping holidays/weekends. Returns 0 on error,
 *   unknown exchange, or if no open time exists within the
 *   registry's generation_range (data eventually runs out -- this
 *   is checked, not assumed infinite).
 */
time_t calendar_next_open_time(const char *exchange);

/*
 * calendar_next_close_time(exchange)
 *   Unix timestamp (UTC) of the next market close (regular close,
 *   or early-close time on an early-close day) at or after now.
 *   Returns 0 on the same error conditions as calendar_next_open_time().
 */
time_t calendar_next_close_time(const char *exchange);

/* ── Utility ───────────────────────────────────────────────────── */

/*
 * calendar_list_exchanges()
 *   Print every loaded exchange (MIC code, name, timezone) to
 *   stderr. Used by the `calendar list` built-in.
 */
void calendar_list_exchanges(void);

#endif /* LAS_SHELL_CALENDAR_H */
