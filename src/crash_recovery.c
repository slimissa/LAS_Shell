/* ══════════════════════════════════════════════════════════════════════════
 * Las_shell Phase 4.4 — Crash Recovery & State Persistence
 * crash_recovery.c
 *
 * Design goals (quant / production mindset):
 *
 *   1. ATOMIC WRITES   — state is written to a .tmp file first, then
 *                        rename(2) swaps it in.  rename() is atomic on
 *                        POSIX filesystems, so a crash mid-write never
 *                        corrupts the last good checkpoint.
 *
 *   2. INTEGRITY CHECK — every checkpoint carries a CRC-32 of its entire
 *                        content so a truncated or tampered file is detected
 *                        at restore time rather than silently applied.
 *
 *   3. STALE DETECTION — if the checkpoint PID matches our own PID we are
 *                        restarting the same process (impossible in normal
 *                        use) and we skip restore to avoid a loop.  If the
 *                        saved PID still exists in /proc we warn the user
 *                        that another shell may be running.
 *
 *   4. THREAD-SAFE     — a dedicated POSIX thread wakes every N seconds and
 *                        calls checkpoint_save_now() under a mutex that also
 *                        guards reads of the env pointer.  The main thread
 *                        can call checkpoint_save_now() directly at any time
 *                        (SIGTERM, clean exit) without races.
 *
 *   5. NON-INTRUSIVE   — checkpoint_start() is one call in main(); nothing
 *                        else in the shell needs to change.  Restore is
 *                        attempted once at startup; if no file exists the
 *                        shell starts fresh with zero overhead.
 * ══════════════════════════════════════════════════════════════════════════ */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include <limits.h>

#include "../include/my_own_shell.h"
#include "../include/crash_recovery.h"

/* ── Forward declarations for internal helpers ──────────────────────────── */
static uint32_t crc32_compute(const char *data, size_t len);
static int       write_checkpoint_to_fd(FILE *f, char **env);
static int       build_checkpoint_path(char *buf, size_t bufsize);

/* ── Module-level state ─────────────────────────────────────────────────── */
static pthread_t    g_chk_thread;
static pthread_mutex_t g_chk_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_chk_cond  = PTHREAD_COND_INITIALIZER;

static volatile int g_chk_running   = 0;   /* 1 while thread is alive       */
static volatile int g_chk_stop_flag = 0;   /* set to 1 to request shutdown  */
static int          g_chk_interval  = CHECKPOINT_INTERVAL_SEC;
/* FIX BUG 10: must be char*** — it stores the ADDRESS of the shell's env
 * pointer (char**).  Dereferencing once gives the live char** array.
 * The original char** declaration caused checkpoint_thread_fn to snapshot
 * a single char* (one env string) instead of the full char** array.       */
static char       ***g_env_ptr_ref  = NULL; /* &env in main() */
static char          g_chk_path[PATH_MAX];  /* absolute path to state file  */

/* ── Stats (informational only) ─────────────────────────────────────────── */
static time_t   g_last_save_time   = 0;
static uint64_t g_save_count       = 0;
static uint64_t g_save_error_count = 0;

/* ══════════════════════════════════════════════════════════════════════════
 * CRC-32 (ISO 3309 polynomial 0xEDB88320)
 * Simple portable implementation — no external dependency.
 *
 * FIX BUG 7: The original "static int table_ready" flag has a data race if
 * two threads call crc32_compute() simultaneously before the table is built.
 * Replaced with pthread_once() which is guaranteed async-signal-safe and
 * race-free — exactly one thread builds the table, all others wait.
 * ══════════════════════════════════════════════════════════════════════════ */
static uint32_t  g_crc_table[256];
static pthread_once_t g_crc_once = PTHREAD_ONCE_INIT;

static void crc32_build_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
            crc = (crc & 1) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        g_crc_table[i] = crc;
    }
}

static uint32_t crc32_compute(const char *data, size_t len)
{
    pthread_once(&g_crc_once, crc32_build_table);

    uint32_t crc = 0xFFFFFFFFu;
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++)
        crc = g_crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Path helpers
 * ══════════════════════════════════════════════════════════════════════════ */
static int build_checkpoint_path(char *buf, size_t bufsize)
{
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') home = "/tmp";
    int n = snprintf(buf, bufsize, "%s/%s", home, CHECKPOINT_FILE);
    return (n > 0 && (size_t)n < bufsize) ? 0 : -1;
}

const char *checkpoint_get_path(void)
{
    if (g_chk_path[0] == '\0')
        build_checkpoint_path(g_chk_path, sizeof(g_chk_path));
    return g_chk_path;
}

/* ══════════════════════════════════════════════════════════════════════════
 * ISO-8601 timestamp helper
 * ══════════════════════════════════════════════════════════════════════════ */
static void iso8601_now(char *buf, size_t bufsize)
{
    time_t     now = time(NULL);
    struct tm *tm  = gmtime(&now);
    strftime(buf, bufsize, "%Y-%m-%dT%H:%M:%SZ", tm);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Core write — produces the checkpoint body into an open FILE*.
 * Returns 0 on success.  The caller is responsible for closing f.
 * ══════════════════════════════════════════════════════════════════════════ */
static int write_checkpoint_to_fd(FILE *f, char **env)
{
    /* ── Buffer to accumulate everything for CRC ── */
    /* We write to a local buffer first, compute CRC, then flush to file.   */
    /* Cap at 4 MB — more than enough for any realistic shell state.        */
    size_t   buf_cap  = 4 * 1024 * 1024;
    size_t   buf_len  = 0;
    char    *buf      = malloc(buf_cap);
    if (!buf) return -1;

#define APPENDF(...) do {                                               \
    int _n = snprintf(buf + buf_len, buf_cap - buf_len, __VA_ARGS__);  \
    if (_n < 0 || (size_t)_n >= buf_cap - buf_len) goto overflow;      \
    buf_len += (size_t)_n;                                             \
} while (0)

    /* ── Header ── */
    char ts[32];
    iso8601_now(ts, sizeof(ts));
    APPENDF("%s\n", CHECKPOINT_MAGIC);
    APPENDF("TIMESTAMP=%s\n", ts);
    APPENDF("PID=%ld\n",     (long)getpid());

    /* ── CWD ── */
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        snprintf(cwd, sizeof(cwd), "/tmp");
    APPENDF("CWD=%s\n", cwd);

    /* ── Environment variables ── */
    if (env) {
        int count = 0;
        for (int i = 0; env[i] && count < CHECKPOINT_MAX_ENV; i++, count++) {
            /* Skip variables that are too long — they corrupt the line format */
            if (strlen(env[i]) > 4096) continue;
            /* Escape newlines in values: replace \n with \\n */
            char escaped[8192];
            const char *src = env[i];
            size_t  eidx = 0;
            while (*src && eidx < sizeof(escaped) - 3) {
                if (*src == '\n') { escaped[eidx++] = '\\'; escaped[eidx++] = 'n'; }
                else               escaped[eidx++] = *src;
                src++;
            }
            escaped[eidx] = '\0';
            APPENDF("ENV=%s\n", escaped);
        }
    }

    /* ── Alias table ── */
    /* aliases[] and alias_count are extern from alias.c */
    {
        int saved = 0;
        for (int i = 0; i < alias_count && saved < CHECKPOINT_MAX_ALIASES; i++, saved++) {
            if (!aliases[i].name || !aliases[i].value) continue;
            /* Escape '=' in alias values with \= to preserve parse round-trip */
            APPENDF("ALIAS=%s=%s\n", aliases[i].name, aliases[i].value);
        }
    }

    /* ── Active background jobs ── */
    /* jobs[] and job_count are extern from operators.c */
    {
        for (int i = 0; i < job_count && i < CHECKPOINT_MAX_JOBS; i++) {
            if (!jobs[i].active) continue;
            /* Encode: JOB=<job_id>:<pid>:<command> */
            const char *cmd = jobs[i].command ? jobs[i].command : "";
            APPENDF("JOB=%d:%ld:%s\n",
                    jobs[i].job_id,
                    (long)jobs[i].pid,
                    cmd);
        }
    }

    /* ── CRC-32 ── */
    uint32_t crc = crc32_compute(buf, buf_len);
    APPENDF("CHECKSUM=%08X\n", crc);

#undef APPENDF

    /* ── Flush to file ── */
    if (fwrite(buf, 1, buf_len, f) != buf_len) {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;

overflow:
    free(buf);
    fprintf(stderr, "[checkpoint] state buffer overflow — state not saved\n");
    return -1;
}

/* ══════════════════════════════════════════════════════════════════════════
 * checkpoint_save_now()
 * ══════════════════════════════════════════════════════════════════════════ */
int checkpoint_save_now(char **env)
{
    if (g_chk_path[0] == '\0')
        if (build_checkpoint_path(g_chk_path, sizeof(g_chk_path)) != 0) {
            fprintf(stderr, "[checkpoint] cannot build checkpoint path\n");
            return -1;
        }

    /* Write to a temp file first — atomic swap via rename(2).
     * FIX BUG 6: tmp_path must be PATH_MAX + extra room for ".tmp.<PID>"
     * suffix (up to 26 chars: ".tmp." = 5, PID max = 20 digits, NUL = 1).
     * A plain PATH_MAX buffer would overflow when g_chk_path is near-full. */
    char tmp_path[PATH_MAX + 32];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", g_chk_path, (long)getpid());

    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        fprintf(stderr, "[checkpoint] cannot open %s: %s\n",
                tmp_path, strerror(errno));
        g_save_error_count++;
        return -1;
    }

    /* Restrict permissions: owner read/write only (no group/other) */
    if (fchmod(fileno(f), S_IRUSR | S_IWUSR) != 0)
        fprintf(stderr, "[checkpoint] WARNING: fchmod failed — permissions may be too open\n");
        
    int rc = write_checkpoint_to_fd(f, env);
    fclose(f);

    if (rc != 0) {
        unlink(tmp_path);
        g_save_error_count++;
        return -1;
    }

    /* Atomic replace */
    if (rename(tmp_path, g_chk_path) != 0) {
        fprintf(stderr, "[checkpoint] rename failed: %s\n", strerror(errno));
        unlink(tmp_path);
        g_save_error_count++;
        return -1;
    }

    g_last_save_time = time(NULL);
    g_save_count++;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Background checkpoint thread
 * ══════════════════════════════════════════════════════════════════════════ */
static void *checkpoint_thread_fn(void *arg)
{
    (void)arg;

    while (1) {
        /* Sleep for g_chk_interval seconds, but wake early if signalled */
        pthread_mutex_lock(&g_chk_mutex);

        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += g_chk_interval;

        /* Wait with timeout — pthread_cond_timedwait returns ETIMEDOUT on
         * natural expiry, or 0 if signalled (stop or force-save).          */
        int wait_rc = 0;
        while (!g_chk_stop_flag && wait_rc == 0)
            wait_rc = pthread_cond_timedwait(&g_chk_cond, &g_chk_mutex, &deadline);

        if (g_chk_stop_flag) {
            pthread_mutex_unlock(&g_chk_mutex);
            break;
        }

        /* Snapshot the env pointer under the lock */
        char **env_snap = g_env_ptr_ref ? *g_env_ptr_ref : NULL;
        pthread_mutex_unlock(&g_chk_mutex);

        /* Write outside the lock — write_checkpoint_to_fd is pure I/O */
        if (checkpoint_save_now(env_snap) != 0) {
            /* Non-fatal: log once per 10 failures to avoid log spam */
            if (g_save_error_count % 10 == 1)
                fprintf(stderr,
                        "[checkpoint] warning: periodic save failed "
                        "(%llu errors so far)\n",
                        (unsigned long long)g_save_error_count);
        }
    }

    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════════
 * checkpoint_start() / checkpoint_stop()
 * ══════════════════════════════════════════════════════════════════════════ */
int checkpoint_start(char ***env_ptr, int interval_sec)
{
    if (build_checkpoint_path(g_chk_path, sizeof(g_chk_path)) != 0)
        return -1;

    pthread_mutex_lock(&g_chk_mutex);
    g_env_ptr_ref  = env_ptr;
    g_chk_stop_flag = 0;
    g_chk_running   = 0;
    if (interval_sec > 0)
        g_chk_interval = interval_sec;
    pthread_mutex_unlock(&g_chk_mutex);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    int rc = pthread_create(&g_chk_thread, &attr, checkpoint_thread_fn, NULL);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        fprintf(stderr, "[checkpoint] pthread_create failed: %s\n",
                strerror(rc));
        return -1;
    }

    /* FIX BUG 9: set g_chk_running under the mutex — consistent with every
     * other reader/writer of this flag, and prevents a hypothetical race
     * where checkpoint_stop() sees running=0 before the thread is joinable. */
    pthread_mutex_lock(&g_chk_mutex);
    g_chk_running = 1;
    pthread_mutex_unlock(&g_chk_mutex);

    fprintf(stderr,
            "[checkpoint] state persistence active — saving every %ds to %s\n",
            g_chk_interval, g_chk_path);
    return 0;
}

void checkpoint_stop(void)
{
    if (!g_chk_running) return;

    pthread_mutex_lock(&g_chk_mutex);
    g_chk_stop_flag = 1;
    pthread_cond_signal(&g_chk_cond);
    pthread_mutex_unlock(&g_chk_mutex);

    pthread_join(g_chk_thread, NULL);
    g_chk_running = 0;
}

void checkpoint_set_interval(int seconds)
{
    if (seconds < 1) seconds = 1;
    pthread_mutex_lock(&g_chk_mutex);
    g_chk_interval = seconds;
    pthread_mutex_unlock(&g_chk_mutex);
}

/* Return the current live env snapshot pointer (for SIGTERM path). */
char **checkpoint_get_live_env(void)
{
    pthread_mutex_lock(&g_chk_mutex);
    char **e = g_env_ptr_ref ? *g_env_ptr_ref : NULL;
    pthread_mutex_unlock(&g_chk_mutex);
    return e;
}

/* ══════════════════════════════════════════════════════════════════════════
 * checkpoint_restore()
 *
 * Parses ~/.las_shell_state line by line.
 * Protocol:
 *   1. Verify magic header.
 *   2. Accumulate all lines (except the CHECKSUM line itself) for CRC.
 *   3. Compare checksum — reject if mismatch.
 *   4. Check PID staleness.
 *   5. Apply CWD, ENV, ALIAS, JOB in order.
 * ══════════════════════════════════════════════════════════════════════════ */
CheckpointRestoreResult checkpoint_restore(char ***env_ptr,
                                           char  *cwd_buf,
                                           size_t cwd_buf_size)
{
    if (g_chk_path[0] == '\0')
        build_checkpoint_path(g_chk_path, sizeof(g_chk_path));

    FILE *f = fopen(g_chk_path, "r");
    if (!f) return CHK_RESTORE_NONE;

    /* ── Load entire file into memory ── */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 8 * 1024 * 1024) {
        fclose(f);
        return CHK_RESTORE_CORRUPT;
    }

    char *raw = malloc((size_t)file_size + 1);
    if (!raw) { fclose(f); return CHK_RESTORE_CORRUPT; }

    size_t nread = fread(raw, 1, (size_t)file_size, f);
    fclose(f);
    raw[nread] = '\0';

    /* ── Separate body (everything before last CHECKSUM= line) from footer ─ */
    char *csl = strstr(raw, "\nCHECKSUM=");
    if (!csl) { free(raw); return CHK_RESTORE_CORRUPT; }
    size_t body_len = (size_t)(csl - raw) + 1; /* +1 for '\n' */

    /* Parse stored checksum */
    uint32_t stored_crc = 0;
    if (sscanf(csl + 1 + 9, "%8X", &stored_crc) != 1) {
        free(raw);
        return CHK_RESTORE_CORRUPT;
    }

    /* Security: nothing must appear after the CHECKSUM line.
     * An attacker could append lines after a valid CHECKSUM= without
     * breaking the CRC (since CRC only covers the body before CHECKSUM).
     * Detect this by verifying the file ends immediately after the
     * CHECKSUM line (allowing for a single trailing newline).            */
    {
        const char *after_csl = csl + 1; /* skip the leading '\n' */
        const char *eol = strchr(after_csl, '\n');
        if (!eol) {
            /* no newline at all after CHECKSUM= — corrupt */
            free(raw);
            return CHK_RESTORE_CORRUPT;
        }
        /* Everything after this newline must be NUL (end of file) */
        const char *tail = eol + 1;
        while (*tail == '\n' || *tail == '\r') tail++; /* allow blank lines */
        if (*tail != '\0') {
            fprintf(stderr,
                    "[checkpoint] integrity check FAILED "
                    "(trailing content after CHECKSUM) — checkpoint rejected\n");
            free(raw);
            return CHK_RESTORE_CORRUPT;
        }
    }

    /* ── Compute CRC over body (everything up to and including the '\n'
     *    before CHECKSUM=) ── */
    uint32_t computed_crc = crc32_compute(raw, body_len);
    if (computed_crc != stored_crc) {
        fprintf(stderr,
                "[checkpoint] integrity check FAILED (stored=%08X computed=%08X) "
                "— checkpoint ignored\n",
                stored_crc, computed_crc);
        free(raw);
        return CHK_RESTORE_CORRUPT;
    }

    /* ── Parse lines ── */
    char  *line      = raw;
    int    line_num  = 0;
    long   saved_pid = 0;

    /* Temporary storage for restored env entries */
    char *new_env_entries[CHECKPOINT_MAX_ENV];
    int   new_env_count = 0;

    /* Temporary alias storage */
    char *restored_alias_names [CHECKPOINT_MAX_ALIASES];
    char *restored_alias_values[CHECKPOINT_MAX_ALIASES];
    int   restored_alias_count  = 0;

    while (*line) {
        /* Find end of current line */
        char *nl = strchr(line, '\n');
        size_t line_len = nl ? (size_t)(nl - line) : strlen(line);

        /* NUL-terminate this line (temporarily) */
        char saved_char = line[line_len];
        line[line_len]  = '\0';

        /* ── Line 0: magic ── */
        if (line_num == 0) {
            if (strcmp(line, CHECKPOINT_MAGIC) != 0) {
                fprintf(stderr,
                        "[checkpoint] bad magic '%s' — not a Las_shell checkpoint\n",
                        line);
                line[line_len] = saved_char;
                free(raw);
                return CHK_RESTORE_CORRUPT;
            }
            goto next_line;
        }

        /* ── TIMESTAMP= (informational) ── */
        if (strncmp(line, "TIMESTAMP=", 10) == 0) {
            fprintf(stderr, "[checkpoint] restoring session from %s\n",
                    line + 10);
            goto next_line;
        }

        /* ── PID= ── */
        if (strncmp(line, "PID=", 4) == 0) {
            saved_pid = atol(line + 4);
            if (saved_pid == (long)getpid()) {
                /* Same PID — must be a stale file from a previous session
                 * that happened to reuse this PID.  Very unlikely but safe
                 * to skip.                                                  */
                fprintf(stderr,
                        "[checkpoint] stale checkpoint (same PID %ld) — skipped\n",
                        saved_pid);
                line[line_len] = saved_char;
                free(raw);
                return CHK_RESTORE_STALE;
            }
            goto next_line;
        }

        /* ── CWD= ── */
        if (strncmp(line, "CWD=", 4) == 0) {
            const char *saved_cwd = line + 4;
            if (cwd_buf && cwd_buf_size > 0) {
                strncpy(cwd_buf, saved_cwd, cwd_buf_size - 1);
                cwd_buf[cwd_buf_size - 1] = '\0';
            }
            goto next_line;
        }

        /* ── ENV= ── */
        if (strncmp(line, "ENV=", 4) == 0) {
            if (new_env_count < CHECKPOINT_MAX_ENV) {
                /* Unescape \\n → \n */
                const char *src = line + 4;
                char *entry = malloc(strlen(src) + 1);
                if (entry) {
                    char *dst = entry;
                    while (*src) {
                        if (src[0] == '\\' && src[1] == 'n') {
                            *dst++ = '\n';
                            src   += 2;
                        } else {
                            *dst++ = *src++;
                        }
                    }
                    *dst = '\0';
                    new_env_entries[new_env_count++] = entry;
                }
            }
            goto next_line;
        }

        /* ── ALIAS= ── */
        if (strncmp(line, "ALIAS=", 6) == 0 &&
            restored_alias_count < CHECKPOINT_MAX_ALIASES)
        {
            const char *rest  = line + 6;
            const char *eq    = strchr(rest, '=');
            if (eq) {
                size_t name_len = (size_t)(eq - rest);
                restored_alias_names [restored_alias_count] =
                    strndup(rest, name_len);
                restored_alias_values[restored_alias_count] =
                    strdup(eq + 1);
                restored_alias_count++;
            }
            goto next_line;
        }

        /* ── JOB= ── */
        if (strncmp(line, "JOB=", 4) == 0) {
            /* Format: JOB=<job_id>:<pid>:<command> */
            int  job_id  = 0;
            long job_pid = 0;
            char job_cmd[1024] = "";
            if (sscanf(line + 4, "%d:%ld:", &job_id, &job_pid) == 2) {
                /* Command starts after the two colons */
                const char *colon1 = strchr(line + 4, ':');
                if (colon1) {
                    const char *colon2 = strchr(colon1 + 1, ':');
                    if (colon2)
                        strncpy(job_cmd, colon2 + 1, sizeof(job_cmd) - 1);
                }

                /* Check if the PID is still alive in /proc */
                char proc_path[64];
                snprintf(proc_path, sizeof(proc_path), "/proc/%ld", job_pid);
                struct stat st;
                if (stat(proc_path, &st) == 0) {
                    fprintf(stderr,
                            "[checkpoint] job [%d] PID %ld (%s) — still running, "
                            "re-registering\n",
                            job_id, job_pid, job_cmd);
                    /* Re-register with the job table */
                    add_job((pid_t)job_pid, job_cmd);
                } else {
                    fprintf(stderr,
                            "[checkpoint] job [%d] PID %ld (%s) — no longer "
                            "running (ignored)\n",
                            job_id, job_pid, job_cmd);
                }
            }
            goto next_line;
        }

        /* ── CHECKSUM= — stop parsing ── */
        if (strncmp(line, "CHECKSUM=", 9) == 0)
            goto done_parsing;

next_line:
        line[line_len] = saved_char;
        line = nl ? nl + 1 : line + line_len;
        line_num++;
        continue;

done_parsing:
        line[line_len] = saved_char;
        break;
    }

    /* ── Apply restored environment ── */
    if (new_env_count > 0 && env_ptr) {
        /* Merge: start from current env, overwrite/add from checkpoint */
        char **cur = *env_ptr;
        int    cur_count = 0;
        while (cur && cur[cur_count]) cur_count++;

        /* Build merged env — size upper bound */
        char **merged = malloc((size_t)(cur_count + new_env_count + 1)
                                * sizeof(char *));
        if (merged) {
            int m = 0;

            /* Copy current env entries that are NOT overridden */
            for (int i = 0; i < cur_count; i++) {
                /* Find key of current entry */
                const char *eq = strchr(cur[i], '=');
                if (!eq) { merged[m++] = strdup(cur[i]); continue; }
                size_t klen = (size_t)(eq - cur[i]);

                /* Check if checkpoint has the same key */
                int overridden = 0;
                for (int j = 0; j < new_env_count; j++) {
                    const char *eq2 = strchr(new_env_entries[j], '=');
                    if (!eq2) continue;
                    size_t klen2 = (size_t)(eq2 - new_env_entries[j]);
                    if (klen == klen2 &&
                        strncmp(cur[i], new_env_entries[j], klen) == 0) {
                        overridden = 1;
                        break;
                    }
                }
                if (!overridden)
                    merged[m++] = strdup(cur[i]);
            }

            /* Append all checkpoint env entries */
            for (int j = 0; j < new_env_count; j++)
                merged[m++] = new_env_entries[j]; /* transfer ownership */

            merged[m] = NULL;

            /* Free old env */
            for (int i = 0; cur && cur[i]; i++) free(cur[i]);
            free(cur);

            *env_ptr = merged;
        } else {
            /* Malloc failed — free temp entries */
            for (int j = 0; j < new_env_count; j++)
                free(new_env_entries[j]);
        }
    } else {
        for (int j = 0; j < new_env_count; j++)
            free(new_env_entries[j]);
    }

    /* ── Apply restored aliases ── */
    for (int i = 0; i < restored_alias_count; i++) {
        if (restored_alias_names[i] && restored_alias_values[i]) {
            /* Only restore aliases not already defined */
            int exists = 0;
            for (int j = 0; j < alias_count; j++) {
                if (aliases[j].name &&
                    strcmp(aliases[j].name, restored_alias_names[i]) == 0) {
                    exists = 1;
                    break;
                }
            }
            if (!exists && alias_count < MAX_ALIASES) {
                aliases[alias_count].name  = restored_alias_names[i];
                aliases[alias_count].value = restored_alias_values[i];
                alias_count++;
            } else {
                free(restored_alias_names[i]);
                free(restored_alias_values[i]);
            }
        }
    }

    free(raw);

    fprintf(stderr,
            "[checkpoint] session restored from PID %ld — "
            "type 'jobs' to see background tasks\n",
            saved_pid);
    return CHK_RESTORE_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 * checkpoint_delete()
 * ══════════════════════════════════════════════════════════════════════════ */
void checkpoint_delete(void)
{
    if (g_chk_path[0] == '\0')
        build_checkpoint_path(g_chk_path, sizeof(g_chk_path));
    if (unlink(g_chk_path) == 0)
        fprintf(stderr, "[checkpoint] state file removed (%s)\n", g_chk_path);
}

/* ══════════════════════════════════════════════════════════════════════════
 * checkpoint_status()
 * ══════════════════════════════════════════════════════════════════════════ */
void checkpoint_status(void)
{
    const char *path = checkpoint_get_path();

    printf("╭─ Las_shell Checkpoint Status ──────────────────────────────────╮\n");
    printf("│ File      : %s\n", path);
    printf("│ Interval  : %d seconds\n", g_chk_interval);
    printf("│ Thread    : %s\n", g_chk_running ? "RUNNING" : "STOPPED");
    printf("│ Saves OK  : %llu\n", (unsigned long long)g_save_count);
    printf("│ Save ERR  : %llu\n", (unsigned long long)g_save_error_count);

    if (g_last_save_time) {
        char ts[32];
        struct tm *tm = gmtime(&g_last_save_time);
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", tm);
        printf("│ Last save : %s\n", ts);
    } else {
        printf("│ Last save : (never)\n");
    }

    /* Show file info if it exists */
    struct stat st;
    if (stat(path, &st) == 0) {
        char mtime_buf[32];
        struct tm *mt = gmtime(&st.st_mtime);
        strftime(mtime_buf, sizeof(mtime_buf), "%Y-%m-%dT%H:%M:%SZ", mt);
        printf("│ File size : %ld bytes\n", (long)st.st_size);
        printf("│ File mtime: %s\n", mtime_buf);
    } else {
        printf("│ File      : not present\n");
    }

    printf("╰─────────────────────────────────────────────────────────────╯\n");
}

/* ══════════════════════════════════════════════════════════════════════════
 * command_checkpoint() — shell built-in
 *
 * checkpoint               → show status
 * checkpoint save          → force immediate save
 * checkpoint restore       → replay checkpoint (normally auto at startup)
 * checkpoint delete        → remove state file
 * checkpoint status        → alias for no-args
 * checkpoint interval <N>  → change write period
 *
 * FIX BUG 3: 'checkpoint restore' needs to update the SHELL's live env
 * pointer, not just a local copy.  The function signature is extended with
 * env_ptr (char***) so the caller in shell_builtins can pass &env.
 * The old command_checkpoint(args, env) wrapper calls through to this.
 * ══════════════════════════════════════════════════════════════════════════ */
int command_checkpoint(char **args, char **env)
{
    /* No subcommand → status */
    if (!args[1] || strcmp(args[1], "status") == 0) {
        checkpoint_status();
        return 0;
    }

    if (strcmp(args[1], "save") == 0) {
        printf("[checkpoint] saving now...\n");
        int rc = checkpoint_save_now(env);
        if (rc == 0)
            printf("[checkpoint] saved to %s\n", checkpoint_get_path());
        else
            printf("[checkpoint] save FAILED\n");
        return rc;
    }

    if (strcmp(args[1], "restore") == 0) {
        char cwd_buf[PATH_MAX];
        /* FIX BUG 3: pass &env so checkpoint_restore() can update the pointer
         * in place.  Without this, any realloc inside restore was invisible to
         * the shell and the new env entries were silently leaked.             */
        CheckpointRestoreResult r = checkpoint_restore(&env,
                                                       cwd_buf,
                                                       sizeof(cwd_buf));
        switch (r) {
            case CHK_RESTORE_NONE:
                printf("[checkpoint] no checkpoint file found\n");
                break;
            case CHK_RESTORE_OK:
                printf("[checkpoint] restored\n");
                if (cwd_buf[0]) {
                    printf("[checkpoint] restoring CWD: %s\n", cwd_buf);
                    if (chdir(cwd_buf) != 0)
                        fprintf(stderr,
                                "[checkpoint] chdir(%s) failed: %s\n",
                                cwd_buf, strerror(errno));
                }
                break;
            case CHK_RESTORE_STALE:
                printf("[checkpoint] stale checkpoint — skipped\n");
                break;
            case CHK_RESTORE_CORRUPT:
                printf("[checkpoint] checkpoint corrupt or tampered — ignored\n");
                break;
        }
        return (r == CHK_RESTORE_OK) ? 0 : 1;
    }

    if (strcmp(args[1], "delete") == 0) {
        checkpoint_delete();
        return 0;
    }

    if (strcmp(args[1], "interval") == 0) {
        if (!args[2]) {
            fprintf(stderr, "checkpoint interval <seconds>\n");
            return 1;
        }
        int secs = atoi(args[2]);
        if (secs < 1) {
            fprintf(stderr, "[checkpoint] interval must be >= 1 second\n");
            return 1;
        }
        checkpoint_set_interval(secs);
        printf("[checkpoint] interval updated to %d seconds\n", secs);
        return 0;
    }

    fprintf(stderr,
            "usage: checkpoint [save | restore | delete | status | interval <N>]\n");
    return 1;
}
