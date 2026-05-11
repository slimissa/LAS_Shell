/* ══════════════════════════════════════════════════════════════════════════
 * Las_shell Phase 4.4 — Crash Recovery & State Persistence
 * crash_recovery.h  — Public API
 *
 * Architecture:
 *   checkpoint_start()      — spawn background thread, writes ~/.las_shell_state
 *                             every CHECKPOINT_INTERVAL_SEC seconds
 *   checkpoint_stop()       — graceful shutdown of the checkpoint thread
 *   checkpoint_save_now()   — force an immediate checkpoint (used on SIGTERM)
 *   checkpoint_restore()    — called at startup; returns 1 if state was loaded
 *   checkpoint_delete()     — clean exit: remove stale state file
 *   command_checkpoint()    — built-in dispatcher:
 *                               checkpoint save | restore | status | delete
 *
 * State file format (JSON-like, human-readable, line-oriented):
 *   Line 1 : LAS_SHELL_CHECKPOINT_V1
 *   Line 2 : TIMESTAMP=<ISO-8601>
 *   Line 3 : PID=<shell-pid>
 *   Line 4 : CWD=<absolute-path>
 *   Line 5+: ENV=<key>=<value>          (one per line)
 *   ...    : ALIAS=<name>=<value>       (one per line)
 *   ...    : JOB=<job_id>:<pid>:<cmd>   (one per line, active only)
 *   Last   : CHECKSUM=<crc32-hex>
 *
 * Thread-safety: all shared state is protected by g_chk_mutex.
 *                env pointer is snapshotted under the lock.
 * ══════════════════════════════════════════════════════════════════════════ */

#ifndef CRASH_RECOVERY_H
#define CRASH_RECOVERY_H

#include <stdint.h>

/* ── Configuration ──────────────────────────────────────────────────────── */
#define CHECKPOINT_FILE         ".las_shell_state"
#define CHECKPOINT_MAGIC        "LAS_SHELL_CHECKPOINT_V1"
#define CHECKPOINT_INTERVAL_SEC  30          /* write state every N seconds  */
#define CHECKPOINT_MAX_ENV      1024         /* cap on env vars to snapshot  */
#define CHECKPOINT_MAX_ALIASES   256         /* cap on aliases to snapshot   */
#define CHECKPOINT_MAX_JOBS      128         /* cap on jobs to snapshot      */

/* ── Restore result ─────────────────────────────────────────────────────── */
typedef enum {
    CHK_RESTORE_NONE   = 0,   /* no checkpoint file found — fresh start      */
    CHK_RESTORE_OK     = 1,   /* checkpoint loaded successfully               */
    CHK_RESTORE_STALE  = 2,   /* file found but from same PID — skip         */
    CHK_RESTORE_CORRUPT= 3    /* checksum mismatch — file tampered/truncated  */
} CheckpointRestoreResult;

/* ── Public API ─────────────────────────────────────────────────────────── */

/*
 * checkpoint_start(env_ptr, interval_sec)
 *   Spawn the background checkpoint thread.
 *   env_ptr : pointer to the live env array (kept as a reference)
 *   interval : seconds between writes; 0 → use CHECKPOINT_INTERVAL_SEC
 *   Returns 0 on success, -1 if thread creation failed.
 */
int  checkpoint_start(char ***env_ptr, int interval_sec);

/*
 * checkpoint_stop()
 *   Signal the checkpoint thread to exit and join it.
 *   Safe to call even if checkpoint_start() was never called.
 */
void checkpoint_stop(void);

/*
 * checkpoint_save_now(env)
 *   Write a checkpoint synchronously.  Called from SIGTERM handler and
 *   from the "checkpoint save" built-in.
 *   Returns 0 on success, -1 on I/O error.
 */
int  checkpoint_save_now(char **env);

/*
 * checkpoint_restore(env_ptr, cwd_buf, cwd_buf_size)
 *   Read ~/.las_shell_state and restore session.
 *   - env_ptr     : updated with variables found in checkpoint
 *   - cwd_buf     : filled with the saved working directory (may be NULL)
 *   - cwd_buf_size: size of cwd_buf
 *   Returns a CheckpointRestoreResult enum value.
 */
CheckpointRestoreResult checkpoint_restore(char ***env_ptr,
                                           char  *cwd_buf,
                                           size_t cwd_buf_size);

/*
 * checkpoint_delete()
 *   Remove ~/.las_shell_state.  Called on clean exit.
 */
void checkpoint_delete(void);

/*
 * checkpoint_status()
 *   Print a human-readable status block to stdout.
 */
void checkpoint_status(void);

/*
 * command_checkpoint(args, env)
 *   Shell built-in entry point.
 *   Usage: checkpoint [save | restore | delete | status | interval <N>]
 */
int  command_checkpoint(char **args, char **env);

/*
 * checkpoint_set_interval(seconds)
 *   Adjust the interval at runtime (checkpoint interval <N>).
 */
void checkpoint_set_interval(int seconds);
char **checkpoint_get_live_env(void);

/*
 * checkpoint_get_path()
 *   Return the absolute path of the checkpoint file.
 */
const char *checkpoint_get_path(void);

#endif /* CRASH_RECOVERY_H */
