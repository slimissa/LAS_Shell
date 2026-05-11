/*
 * streaming_sub.c — $<() Streaming Substitution for Las_shell
 *
 * Design contract (from roadmap §3.1):
 *   - $<(source) opens a named pipe to the source command
 *   - Each call to stream_sub_read_line() returns ONE line from that pipe
 *   - Designed for use inside while loops: each iteration gets a fresh line
 *   - Cleanup: stream_sub_close() reaps the child and unlinks the fifo
 *
 * Architecture:
 *   StreamSub {
 *       int   read_fd;      // O_RDONLY end of the FIFO
 *       pid_t source_pid;   // child writing to FIFO
 *       char  pipe_path[];  // path to the mkfifo node
 *   }
 *
 *   The source child is forked with its stdout→FIFO write-end.
 *   The parent opens the FIFO read-end after the child is running.
 *   This avoids a deadlock: open(FIFO, O_RDONLY) blocks until a writer
 *   exists, and open(FIFO, O_WRONLY) blocks until a reader exists —
 *   so we fork the writer first, then open the reader.
 *
 *   For infinite sources (e.g. a price daemon), the FIFO stays open
 *   until stream_sub_close() is called (typically on loop exit / SIGINT).
 *   For finite sources (e.g. seq 1 5), EOF on the read side returns NULL,
 *   which the while-loop interpreter treats as loop termination.
 *
 * Thread-safety: one StreamSub per $<() expression. If a script has
 * two $<() calls in the same line, each gets its own registry entry
 * (managed by the registry below).
 */

#define _POSIX_C_SOURCE 200809L

#include "../include/my_own_shell.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

/* ── Registry ──────────────────────────────────────────────────────────────
 * Keeps all live StreamSub handles so that stream_sub_close_all() can
 * sweep them on SIGINT or normal shell exit.
 */
#define MAX_STREAM_SUBS 32

static StreamSub* g_registry[MAX_STREAM_SUBS];
static int        g_registry_count = 0;

static void registry_add(StreamSub* ss) {
    if (g_registry_count < MAX_STREAM_SUBS)
        g_registry[g_registry_count++] = ss;
}

static void registry_remove(StreamSub* ss) {
    for (int i = 0; i < g_registry_count; i++) {
        if (g_registry[i] == ss) {
            g_registry[i] = g_registry[--g_registry_count];
            return;
        }
    }
}

/* ── stream_sub_open() ─────────────────────────────────────────────────────
 *
 * Creates the FIFO, forks the source command (which writes to the FIFO),
 * and opens the read end. Returns a heap-allocated StreamSub on success,
 * NULL on any failure.
 *
 * cmd  : the command string inside $<(...)
 * env  : inherited environment for the child
 */
StreamSub* stream_sub_open(const char* cmd, char** env) {
    if (!cmd || !cmd[0]) {
        fprintf(stderr, "$<(): empty command\n");
        return NULL;
    }

    StreamSub* ss = calloc(1, sizeof(StreamSub));
    if (!ss) { perror("$<(): calloc"); return NULL; }

    ss->read_fd    = -1;
    ss->source_pid = -1;
    ss->buf_pos    = 0;
    ss->buf_len    = 0;
    ss->eof        = 0;

    /* ── Build unique FIFO path in /tmp ── */
    static unsigned int _stream_seq = 0;
    snprintf(ss->pipe_path, sizeof(ss->pipe_path),
             "/tmp/las_shell_stream_%d_%ld_%u",
             (int)getpid(), (long)time(NULL), ++_stream_seq);

    /* ── Create the FIFO node ── */
    /* Remove stale node if any (previous crash etc.) */
    unlink(ss->pipe_path);
    if (mkfifo(ss->pipe_path, 0600) != 0) {
        fprintf(stderr, "$<(): mkfifo failed: %s\n", strerror(errno));
        free(ss);
        return NULL;
    }

    /* ── Fork the source writer ──
     *
     * We use O_NONBLOCK on the write open so that the child can open the
     * FIFO immediately without waiting for a reader. Once the parent opens
     * the read end the FIFO becomes fully connected.
     *
     * We use /bin/sh -c so the cmd string can be anything the shell
     * understands (pipelines, substitutions, etc.).
     */
    ss->source_pid = fork();
    if (ss->source_pid == -1) {
        perror("$<(): fork");
        unlink(ss->pipe_path);
        free(ss);
        return NULL;
    }

    if (ss->source_pid == 0) {
        /* ── Child: redirect stdout → FIFO write end ── */
        int wfd = open(ss->pipe_path, O_WRONLY);   /* blocks until reader opens */
        if (wfd == -1) {
            perror("$<(): child open(FIFO, WRONLY)");
            exit(1);
        }
        if (dup2(wfd, STDOUT_FILENO) == -1) {
            perror("$<(): child dup2");
            close(wfd);
            exit(1);
        }
        close(wfd);

        /* Propagate environment */
        if (env) {
            extern char** environ;
            environ = env;
        }

        /* Try las_shell -c first (for Las_shell built-ins), fall back to sh */
        char* las_argv[4] = { "./las_shell", "-c", (char*)cmd, NULL };
        execvp(las_argv[0], las_argv);

        char* sh_argv[4]  = { "/bin/sh",    "-c", (char*)cmd, NULL };
        execvp(sh_argv[0], sh_argv);

        fprintf(stderr, "$<(): could not exec '%s': %s\n", cmd, strerror(errno));
        exit(127);
    }

    /* ── Parent: open read end (this blocks until child opens write end) ── */
    ss->read_fd = open(ss->pipe_path, O_RDONLY);
    if (ss->read_fd == -1) {
        fprintf(stderr, "$<(): open(FIFO, RDONLY) failed: %s\n", strerror(errno));
        /* Reap the child so we don't leak */
        kill(ss->source_pid, SIGTERM);
        waitpid(ss->source_pid, NULL, 0);
        unlink(ss->pipe_path);
        free(ss);
        return NULL;
    }

    registry_add(ss);
    return ss;
}

/* ── stream_sub_read_line() ────────────────────────────────────────────────
 *
 * Returns the NEXT line from the stream as a malloc'd, newline-stripped
 * string. Returns NULL on EOF or error (both signal "stop iterating").
 *
 * Uses an internal byte buffer inside StreamSub to avoid a syscall per
 * character, but still presents one logical line per call.
 */
char* stream_sub_read_line(StreamSub* ss) {
    if (!ss || ss->read_fd < 0 || ss->eof) return NULL;

    /* Accumulate characters until '\n' or EOF */
    char  line[4096];
    int   line_pos = 0;

    while (line_pos < (int)sizeof(line) - 1) {
        /* Refill buffer when empty */
        if (ss->buf_pos >= ss->buf_len) {
            ssize_t n = read(ss->read_fd, ss->buffer, sizeof(ss->buffer));
            if (n <= 0) {
                /* EOF or error */
                ss->eof = 1;
                if (line_pos > 0) {
                    /* Flush whatever we have (unterminated last line) */
                    line[line_pos] = '\0';
                    return my_strdup(line);
                }
                return NULL;
            }
            ss->buf_len = (int)n;
            ss->buf_pos = 0;
        }

        char c = ss->buffer[ss->buf_pos++];
        if (c == '\n') {
            line[line_pos] = '\0';
            return my_strdup(line);
        }
        /* Strip carriage returns (Windows line endings from scripts) */
        if (c != '\r') {
            line[line_pos++] = c;
        }
    }

    /* Line too long — truncate and return */
    fprintf(stderr, "$<(): WARNING — line truncated at %d bytes\n", line_pos);
    line[line_pos] = '\0';
    return my_strdup(line);
}

/* ── stream_sub_close() ────────────────────────────────────────────────────
 *
 * Closes the read fd, sends SIGTERM to the source if still running,
 * waits for it, and unlinks the FIFO node.
 */
void stream_sub_close(StreamSub* ss) {
    if (!ss) return;

    if (ss->read_fd >= 0) {
        close(ss->read_fd);
        ss->read_fd = -1;
    }

    if (ss->source_pid > 0) {
        /* Give child a chance to exit on its own (it may have already) */
        int status;
        pid_t result = waitpid(ss->source_pid, &status, WNOHANG);
        if (result == 0) {
            /* Still running — send SIGTERM then reap */
            kill(ss->source_pid, SIGTERM);
            /* Short sleep to let it handle the signal */
            struct timespec ts = { 0, 50000000L }; /* 50ms */
            nanosleep(&ts, NULL);
            waitpid(ss->source_pid, &status, WNOHANG);
        }
        ss->source_pid = -1;
    }

    if (ss->pipe_path[0]) {
        unlink(ss->pipe_path);
        ss->pipe_path[0] = '\0';
    }

    registry_remove(ss);
    free(ss);
}

/* ── stream_sub_close_all() ────────────────────────────────────────────────
 *
 * Emergency sweep — called from SIGINT handler and shell exit.
 * Drains the entire registry.
 */
void stream_sub_close_all(void) {
    /* Iterate backwards so registry_remove doesn't skip entries */
    for (int i = g_registry_count - 1; i >= 0; i--) {
        if (g_registry[i]) stream_sub_close(g_registry[i]);
    }
    g_registry_count = 0;
}

/* ── expand_streaming_substitution() ──────────────────────────────────────
 *
 * Called by the substitution engine when it finds $<(cmd) at the top-level
 * of a simple assignment line OUTSIDE a while loop, e.g.:
 *
 *     price=$<(quote AAPL)    # reads ONE line, assigns it, closes stream
 *
 * This is the "one-shot" mode. For the proper streaming mode (while loops),
 * the script interpreter manages the StreamSub lifecycle directly.
 *
 * Returns a malloc'd string with the first line of output, or "" on error.
 */
char* expand_streaming_substitution_oneshot(const char* cmd, char** env) {
    StreamSub* ss = stream_sub_open(cmd, env);
    if (!ss) return my_strdup("");

    char* line = stream_sub_read_line(ss);
    stream_sub_close(ss);

    return line ? line : my_strdup("");
}

/* ── find_streaming_sub() ─────────────────────────────────────────────────
 *
 * Scans `input` for the pattern $<(...) (not inside single quotes).
 * On match writes:
 *   *start_pos : index of the '$'
 *   *end_pos   : index of the closing ')'
 * and returns a malloc'd copy of the command string inside $<(...)
 *
 * Returns NULL if no $<() is found.
 *
 * This is the peer of find_next_substitution() in substitution.c for the
 * regular $() syntax.
 */
char* find_streaming_substitution(const char* input, int* start_pos, int* end_pos) {
    *start_pos = -1;
    *end_pos   = -1;

    if (!input) return NULL;

    int len = (int)strlen(input);
    int in_single_quotes = 0;
    int in_double_quotes = 0;

    for (int i = 0; i < len; i++) {
        if (input[i] == '\'' && !in_double_quotes) {
            in_single_quotes = !in_single_quotes;
            continue;
        }
        if (input[i] == '"' && !in_single_quotes) {
            in_double_quotes = !in_double_quotes;
            continue;
        }
        if (in_single_quotes) continue;

        /* Match: $ < ( */
        if (input[i] == '$' && i + 2 < len
                && input[i + 1] == '<'
                && input[i + 2] == '(') {

            *start_pos = i;
            int depth = 1;
            int j = i + 3;

            while (j < len && depth > 0) {
                if (input[j] == '(') depth++;
                else if (input[j] == ')') depth--;
                j++;
            }

            if (depth == 0) {
                *end_pos = j - 1;                    /* index of closing ')' */
                int cmd_len = j - i - 4;             /* content between $<( and ) */
                if (cmd_len <= 0) return NULL;

                char* cmd = malloc(cmd_len + 1);
                if (!cmd) return NULL;
                strncpy(cmd, input + i + 3, cmd_len);
                cmd[cmd_len] = '\0';
                return cmd;
            }
        }
    }

    return NULL;
}

/* ── process_line_with_streaming_subs() ───────────────────────────────────
 *
 * Top-level entry point used by the substitution layer.
 *
 * Handles the ONE-SHOT case: replaces every $<(cmd) occurrence in `input`
 * with the first line of that command's output, then returns the expanded
 * string (malloc'd).
 *
 * The ITERATIVE case (while loops) is handled directly by the script
 * interpreter in script.c — it keeps the StreamSub open across iterations.
 */
char* process_line_with_streaming_subs(const char* input, char** env) {
    if (!input) return my_strdup("");

    /* Fast path: no $< at all */
    if (!strstr(input, "$<(")) return my_strdup(input);

    char* result = my_strdup(input);
    if (!result) return NULL;

    int max_passes = 8; /* guard against infinite expansion */
    while (max_passes-- > 0 && strstr(result, "$<(")) {
        int start, end;
        char* cmd = find_streaming_substitution(result, &start, &end);
        if (!cmd) break;

        /* Get one line from the stream */
        char* value = expand_streaming_substitution_oneshot(cmd, env);
        free(cmd);
        if (!value) value = my_strdup("");

        /* Splice value into result in place of $<(cmd) */
        size_t prefix_len = (size_t)start;
        size_t suffix_len = strlen(result) - (size_t)(end + 1);
        size_t val_len    = strlen(value);
        size_t new_len    = prefix_len + val_len + suffix_len + 1;

        char* new_result = malloc(new_len);
        if (!new_result) { free(value); free(result); return NULL; }

        memcpy(new_result,               result,          prefix_len);
        memcpy(new_result + prefix_len,  value,           val_len);
        memcpy(new_result + prefix_len + val_len,
               result + end + 1, suffix_len);
        new_result[new_len - 1] = '\0';

        free(value);
        free(result);
        result = new_result;
    }

    return result;
}
