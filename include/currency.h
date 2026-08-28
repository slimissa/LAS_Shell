#ifndef LAS_SHELL_CURRENCY_H
#define LAS_SHELL_CURRENCY_H

/* ============================================================
 * Las_shell v0.6.0 Milestone 3 — ISO 4217 Currency Library
 * include/currency.h
 *
 * Wraps the iso4217.json registry (github.com/slimissa/iso4217).
 * Only ACTIVE ISO 4217 currencies are loaded into the lookup table
 * (registry's `currencies.active` array) — withdrawn currencies and
 * non-ISO entries (crypto, stablecoins, commodities, IMF SDR-style
 * "special_purpose" codes) are deliberately excluded from
 * currency_lookup()/currency_is_valid(), per the v0.6.0 roadmap:
 * "Only active ISO 4217 currencies in the lookup table."
 *
 * Registry file location, checked in order:
 *   1. Explicit path argument to currency_init()
 *   2. $LAS_SHELL_HOME/iso4217.json
 *   3. /usr/local/share/las_shell/iso4217.json
 *   4. Built-in single-entry fallback (USD only) + stderr warning
 * ============================================================ */

/* ── Currency record ──────────────────────────────────────── */
typedef struct {
    char code[8];          /* "USD" */
    char numeric[4];       /* "840" */
    char name[64];         /* "US Dollar" */
    int  minor_units;      /* 2 for USD, 0 for JPY, 3 for KWD */
    char symbol[8];        /* "$" (UTF-8, may be multi-byte) */
    int  is_active;        /* always 1 for anything in the lookup table */
} Currency;

/* ── Lifecycle ─────────────────────────────────────────────── */

/*
 * currency_init(path)
 *   Load and index the active-currency table.
 *   path may be NULL — in that case the $LAS_SHELL_HOME and
 *   /usr/local/share/las_shell fallback locations are tried.
 *   Returns 0 on success (including the built-in fallback path —
 *   that is a degraded-but-successful load, not a failure), or
 *   -1 only if called with a state that can't even fall back
 *   (should not happen in practice).
 */
int currency_init(const char *path);

/*
 * currency_cleanup()
 *   Free all resources. Safe to call even if currency_init() was
 *   never called or already failed.
 */
void currency_cleanup(void);

/* ── Lookup ────────────────────────────────────────────────── */

/*
 * currency_lookup(code)
 *   Case-insensitive lookup by 3-letter code. Returns a pointer into
 *   the internal table (do not free) or NULL if not found / not an
 *   active ISO 4217 currency.
 */
const Currency* currency_lookup(const char *code);

/*
 * currency_is_valid(code)
 *   Returns 1 if code is a currently-active ISO 4217 currency, 0
 *   otherwise (including NULL/empty input).
 */
int currency_is_valid(const char *code);

/* ── Conversion ────────────────────────────────────────────── */

/*
 * currency_to_minor(code, amount)
 *   Convert a decimal amount to integer minor units using the
 *   currency's minor_units (100.50 USD -> 10050; 500 JPY -> 500;
 *   1.234 KWD -> 1234). Rounds half away from zero. If code is not
 *   a recognized active currency, assumes 2 minor units (defensive
 *   default, matches the most common case) and still returns a
 *   best-effort value rather than aborting -- callers that care
 *   should check currency_is_valid() first.
 */
long long currency_to_minor(const char *code, double amount);

/*
 * currency_from_minor(code, minor)
 *   Inverse of currency_to_minor(). Same fallback behavior for an
 *   unrecognized code.
 */
double currency_from_minor(const char *code, long long minor);

/* ── Display ───────────────────────────────────────────────── */

/*
 * currency_list_all()
 *   Print every active currency (code, name, minor_units, symbol)
 *   to stderr, one per line, sorted by code. Used by the
 *   `currency list` built-in.
 */
void currency_list_all(void);

/* Phase 2 built-in: `currency list|show|validate|minor|format` */
int command_currency(char **args, char **env);

#endif /* LAS_SHELL_CURRENCY_H */
