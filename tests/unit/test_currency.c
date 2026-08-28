/* ============================================================
 * Las_shell v0.6.0 Milestone 3 — Currency Library Unit Tests
 * test_currency.c
 *
 * Standalone test binary. Does NOT require the rest of Las_shell.
 * Compile:
 *   gcc -Wall -Wextra -g -I../../include \
 *       test_currency.c ../../src/currency.c \
 *       -lm -o test_currency
 * Run:
 *   ./test_currency /path/to/iso4217.json
 *
 * Exit 0 = all tests passed. Non-zero = failure count.
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "currency.h"

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
    const char *registry_path = (argc > 1) ? argv[1] : "iso4217.json";

    SECTION("currency_init — valid registry");
    int rc = currency_init(registry_path);
    TEST("currency_init loads valid iso4217.json", rc == 0);
    TEST("USD is valid after real load", currency_is_valid("USD"));
    /* If this fires, every test below is meaningless -- fail loud. */
    if (!currency_is_valid("USD")) {
        fprintf(stderr, "FATAL: could not load %s -- pass the real path "
                        "as argv[1]\n", registry_path);
    }

    SECTION("currency_lookup — known codes");
    const Currency *usd = currency_lookup("USD");
    TEST("currency_lookup finds USD", usd != NULL);
    if (usd) {
        TEST("USD code correct",         strcmp(usd->code, "USD") == 0);
        TEST("USD numeric == 840",       strcmp(usd->numeric, "840") == 0);
        TEST("USD minor_units == 2",     usd->minor_units == 2);
        TEST("USD symbol == $",          strcmp(usd->symbol, "$") == 0);
    }

    const Currency *eur = currency_lookup("EUR");
    TEST("currency_lookup finds EUR", eur != NULL);

    const Currency *jpy = currency_lookup("JPY");
    TEST("currency_lookup finds JPY", jpy != NULL);
    if (jpy) TEST("JPY minor_units == 0", jpy->minor_units == 0);

    const Currency *kwd = currency_lookup("KWD");
    TEST("currency_lookup finds KWD", kwd != NULL);
    if (kwd) TEST("KWD minor_units == 3", kwd->minor_units == 3);

    TEST("currency_lookup is case-insensitive", currency_lookup("usd") != NULL);

    SECTION("currency_lookup — invalid code");
    TEST("currency_lookup returns NULL for invalid code",
         currency_lookup("XXQ") == NULL);
    TEST("currency_lookup returns NULL for empty string",
         currency_lookup("") == NULL);
    TEST("currency_lookup returns NULL for NULL",
         currency_lookup(NULL) == NULL);

    SECTION("currency_is_valid");
    TEST("USD is valid",   currency_is_valid("USD") == 1);
    TEST("XXX is not valid (not an active ISO code)",
         currency_is_valid("XXX") == 0);

    SECTION("currency_to_minor — decimal places by currency");
    TEST("JPY 500 -> 500 (0 decimals)",
         currency_to_minor("JPY", 500.0) == 500);
    TEST("USD 100.50 -> 10050 (2 decimals)",
         currency_to_minor("USD", 100.50) == 10050);
    TEST("KWD 1.234 -> 1234 (3 decimals)",
         currency_to_minor("KWD", 1.234) == 1234);

    SECTION("currency_from_minor — reverses currency_to_minor");
    TEST("USD 10050 minor -> 100.50",
         currency_from_minor("USD", 10050) == 100.50);
    TEST("JPY 500 minor -> 500.0",
         currency_from_minor("JPY", 500) == 500.0);
    TEST("KWD 1234 minor -> 1.234",
         currency_from_minor("KWD", 1234) == 1.234);
    TEST("round-trip USD 100.50 -> minor -> back",
         currency_from_minor("USD", currency_to_minor("USD", 100.50)) == 100.50);

    SECTION("Round half away from zero");
    TEST("2.5 -> 3 (0-decimal currency)",
         currency_to_minor("JPY", 2.5) == 3);
    TEST("-2.5 -> -3 (0-decimal currency)",
         currency_to_minor("JPY", -2.5) == -3);
    TEST("0.5 -> 1 (0-decimal currency)",
         currency_to_minor("JPY", 0.5) == 1);
    TEST("-0.5 -> -1 (0-decimal currency)",
         currency_to_minor("JPY", -0.5) == -1);
    TEST("100.005 -> 10001 rounds away from zero (2-decimal)",
         currency_to_minor("USD", 100.005) == 10001);
    TEST("-100.005 -> -10001 rounds away from zero (2-decimal)",
         currency_to_minor("USD", -100.005) == -10001);

    SECTION("currency_list_all — smoke test (visual, always passes if no crash)");
    currency_list_all();
    TEST("currency_list_all did not crash", 1);

    currency_cleanup();

    SECTION("currency_init_missing — falls back to USD-only");
    rc = currency_init("/nonexistent/path/does/not/exist.json");
    TEST("currency_init succeeds (degraded) on missing file", rc == 0);
    TEST("USD still valid after fallback", currency_is_valid("USD") == 1);
    TEST("EUR not available in USD-only fallback",
         currency_is_valid("EUR") == 0);
    currency_cleanup();

    printf("\n============================================\n");
    printf("Total: %d  Passed: %d  Failed: %d\n", g_total, g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
