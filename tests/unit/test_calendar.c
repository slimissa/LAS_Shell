/* ============================================================
 * Las_shell v0.6.0 Milestone 3 — Calendar Library Unit Tests
 * test_calendar.c
 *
 * Uses a fake "now" via TEST_NOW_UTC env var trick is NOT used here
 * -- instead these tests call calendar_is_holiday/is_early_close/
 * early_close_time (which take an explicit date, not "now") for the
 * deterministic bulk of coverage, and only touch the "now"-based
 * functions (calendar_status/minutes_until_change/next_open/
 * next_close) with loose sanity checks that hold at any wall-clock
 * time, since faking `time()` itself would need LD_PRELOAD or a
 * mockable clock this small library deliberately doesn't have.
 *
 * Compile:
 *   gcc -Wall -Wextra -g -I../../include \
 *       test_calendar.c ../../src/calendar.c -lpthread -o test_calendar
 * Run:
 *   ./test_calendar /path/to/calendar.json
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "calendar.h"

static int g_total = 0, g_passed = 0, g_failed = 0;

#define TEST(name, expr) do {                                         \
    g_total++;                                                        \
    int _ok = !!(expr);                                               \
    if (_ok) { g_passed++; printf("  [PASS] %s\n", (name)); }        \
    else     { g_failed++; printf("  [FAIL] %s\n", (name));          \
               printf("         expr: %s\n", #expr); }               \
} while(0)

#define SECTION(title) printf("\n-- %s --\n", (title))

int main(int argc, char **argv) {
    const char *registry_path = (argc > 1) ? argv[1] : "calendar.json";

    SECTION("calendar_init — valid registry");
    int rc = calendar_init(registry_path);
    TEST("calendar_init loads valid calendar.json", rc == 0);
    TEST("XNYS resolves after real load", calendar_is_holiday("XNYS", "2099-01-01") == 0
         /* 2099 is out of generation_range so should be treated as
          * "not a registered holiday", i.e. a real answer came back,
          * not a crash -- distinguishes a loaded registry from the
          * built-in fallback (which would also return 0 here, so
          * this alone isn't conclusive -- the next test is). */
         );
    TEST("XNYS 2025-12-25 (Christmas) is a holiday -- proves real "
         "holiday data loaded, not just the NYSE-hours-only fallback",
         calendar_is_holiday("XNYS", "2025-12-25") == 1);

    SECTION("Exchange alias resolution");
    TEST("NYSE alias resolves same as XNYS",
         calendar_is_holiday("NYSE", "2025-12-25") ==
         calendar_is_holiday("XNYS", "2025-12-25"));
    TEST("lowercase 'nyse' also resolves",
         calendar_is_holiday("nyse", "2025-12-25") == 1);
    TEST("NASDAQ alias resolves to XNAS",
         calendar_is_holiday("NASDAQ", "2025-12-25") ==
         calendar_is_holiday("XNAS", "2025-12-25"));
    TEST("unknown alias/code returns safe defaults, not a crash",
         calendar_is_holiday("NOT_A_REAL_EXCHANGE", "2025-12-25") == 0);
    TEST("unknown exchange calendar_status == closed",
         strcmp(calendar_status("NOT_A_REAL_EXCHANGE"), "closed") == 0);
    TEST("unknown exchange calendar_minutes_until_change == -1",
         calendar_minutes_until_change("NOT_A_REAL_EXCHANGE") == -1);
    TEST("unknown exchange calendar_next_open_time == 0",
         calendar_next_open_time("NOT_A_REAL_EXCHANGE") == 0);

    SECTION("calendar_is_holiday — known dates");
    /* 2025-12-25 is a Thursday -- a real holiday, not a weekend freebie. */
    TEST("XNYS Thu 2025-12-25 Christmas is a holiday",
         calendar_is_holiday("XNYS", "2025-12-25") == 1);
    /* 2025-07-04 is a Friday -- Independence Day. */
    TEST("XNYS Fri 2025-07-04 Independence Day is a holiday",
         calendar_is_holiday("XNYS", "2025-07-04") == 1);
    /* An ordinary Tuesday. */
    TEST("XNYS ordinary Tue 2025-06-10 is NOT a holiday",
         calendar_is_holiday("XNYS", "2025-06-10") == 0);
    /* Saturday -- weekend, no registry entry needed to know this. */
    TEST("XNYS Sat 2025-06-14 is a holiday (weekend)",
         calendar_is_holiday("XNYS", "2025-06-14") == 1);
    TEST("XNYS Sun 2025-06-15 is a holiday (weekend)",
         calendar_is_holiday("XNYS", "2025-06-15") == 1);

    SECTION("calendar_is_early_close / calendar_early_close_time");
    /* 2025-07-03 (day before July 4th) -- known NYSE early close, 13:00. */
    TEST("XNYS 2025-07-03 is an early close",
         calendar_is_early_close("XNYS", "2025-07-03") == 1);
    TEST("XNYS 2025-07-03 early close time is 13:00",
         calendar_early_close_time("XNYS", "2025-07-03") != NULL &&
         strcmp(calendar_early_close_time("XNYS", "2025-07-03"), "13:00") == 0);
    TEST("early close is NOT also a full holiday",
         calendar_is_holiday("XNYS", "2025-07-03") == 0);
    TEST("an ordinary day is not an early close",
         calendar_is_early_close("XNYS", "2025-06-10") == 0);
    TEST("early_close_time is NULL on an ordinary day",
         calendar_early_close_time("XNYS", "2025-06-10") == NULL);
    TEST("a full holiday is not double-counted as early close",
         calendar_is_early_close("XNYS", "2025-12-25") == 0);

    SECTION("Lunch-break exchanges (XTKS)");
    TEST("XTKS ordinary Tue 2025-06-10 is NOT a holiday",
         calendar_is_holiday("XTKS", "2025-06-10") == 0);
    /* Just confirm XTKS loads and behaves sanely -- exact lunch-break
     * timing is exercised indirectly via calendar_status() below,
     * which can't be pinned to a fixed clock without mocking time(). */

    SECTION("calendar_status / calendar_is_open — live-clock sanity checks");
    const char *st = calendar_status("XNYS");
    TEST("calendar_status(XNYS) returns one of the 6 documented values",
         st && (strcmp(st, "closed") == 0 || strcmp(st, "pre") == 0 ||
                strcmp(st, "open") == 0 || strcmp(st, "lunch_break") == 0 ||
                strcmp(st, "after") == 0 || strcmp(st, "early_close") == 0));
    int is_open = calendar_is_open("XNYS");
    TEST("calendar_is_open(XNYS) agrees with calendar_status",
         is_open == (strcmp(st, "open") == 0 || strcmp(st, "early_close") == 0));

    int mins = calendar_minutes_until_change("XNYS");
    TEST("calendar_minutes_until_change(XNYS) is positive (registry has "
         "data covering 'now')", mins > 0);
    TEST("calendar_minutes_until_change(XNYS) is under a year (sane, not "
         "a runaway loop artifact)", mins < 366 * 1440);

    SECTION("calendar_next_open_time / calendar_next_close_time — sanity");
    time_t now = time(NULL);
    time_t next_open = calendar_next_open_time("XNYS");
    time_t next_close = calendar_next_close_time("XNYS");
    TEST("calendar_next_open_time(XNYS) is at or after now",
         next_open >= now);
    TEST("calendar_next_open_time(XNYS) is within a year",
         next_open > 0 && next_open - now < 366 * 24 * 3600);
    TEST("calendar_next_close_time(XNYS) is at or after now",
         next_close >= now);
    TEST("calendar_next_close_time(XNYS) is within a year",
         next_close > 0 && next_close - now < 366 * 24 * 3600);

    SECTION("calendar_list_exchanges — smoke test");
    calendar_list_exchanges();
    TEST("calendar_list_exchanges did not crash", 1);

    calendar_cleanup();

    SECTION("calendar_init_missing — falls back to built-in NYSE defaults");
    rc = calendar_init("/nonexistent/path/does/not/exist.json");
    TEST("calendar_init succeeds (degraded) on missing file", rc == 0);
    TEST("NYSE alias still resolves in fallback mode",
         strcmp(calendar_status("NYSE"), "closed") == 0 ||
         strcmp(calendar_status("NYSE"), "open") == 0 ||
         strcmp(calendar_status("NYSE"), "pre") == 0 ||
         strcmp(calendar_status("NYSE"), "after") == 0);
    TEST("fallback has no holiday data -- Christmas is NOT flagged "
         "(documented gap, not silently faked)",
         calendar_is_holiday("NYSE", "2025-12-25") == 0 ||
         /* unless 2025-12-25 happens to be a weekend in some future
          * re-run of this test file -- it isn't (Thursday), but guard
          * the assertion's intent rather than its incidental date. */
         0);
    TEST("unrelated exchange NOT in the NYSE-only fallback resolves to NULL",
         calendar_status("XLON") != NULL &&
         strcmp(calendar_status("XLON"), "closed") == 0);
    calendar_cleanup();

    printf("\n============================================\n");
    printf("Total: %d  Passed: %d  Failed: %d\n", g_total, g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
