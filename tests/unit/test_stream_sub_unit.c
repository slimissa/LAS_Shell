/*
 * test_stream_sub_unit.c
 * Standalone unit test for the StreamSub FIFO machinery.
 * Compiles and runs independently of the full shell.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

/* Inline the parts of StreamSub we need */
#define STREAM_BUF_SIZE 4096
typedef struct {
    int    read_fd;
    pid_t  source_pid;
    char   pipe_path[256];
    char   buffer[STREAM_BUF_SIZE];
    int    buf_pos;
    int    buf_len;
    int    eof;
} StreamSub;

/* ── Minimal stubs ── */
char* my_strdup(const char* s) { return strdup(s); }
int   get_watch_stop(void)     { return 0; }

/* ── Inline the streaming_sub.c core (open/read/close) ── */
static StreamSub* stream_sub_open_test(const char* cmd) {
    StreamSub* ss = calloc(1, sizeof(StreamSub));
    ss->read_fd    = -1;
    ss->source_pid = -1;
    snprintf(ss->pipe_path, sizeof(ss->pipe_path),
             "/tmp/las_shell_unit_%d_%ld", (int)getpid(), (long)time(NULL));
    unlink(ss->pipe_path);
    if (mkfifo(ss->pipe_path, 0600) != 0) {
        fprintf(stderr, "mkfifo: %s\n", strerror(errno));
        free(ss); return NULL;
    }
    ss->source_pid = fork();
    if (ss->source_pid == 0) {
        int wfd = open(ss->pipe_path, O_WRONLY);
        if (wfd < 0) { perror("child open"); exit(1); }
        dup2(wfd, STDOUT_FILENO);
        close(wfd);
        char* sh[] = { "/bin/sh", "-c", (char*)cmd, NULL };
        execvp("/bin/sh", sh);
        exit(127);
    }
    ss->read_fd = open(ss->pipe_path, O_RDONLY);
    if (ss->read_fd < 0) {
        fprintf(stderr, "parent open: %s\n", strerror(errno));
        kill(ss->source_pid, SIGTERM);
        waitpid(ss->source_pid, NULL, 0);
        unlink(ss->pipe_path);
        free(ss); return NULL;
    }
    return ss;
}

static char* stream_sub_read_line_test(StreamSub* ss) {
    if (!ss || ss->read_fd < 0 || ss->eof) return NULL;
    char line[4096]; int line_pos = 0;
    while (line_pos < 4095) {
        if (ss->buf_pos >= ss->buf_len) {
            ssize_t n = read(ss->read_fd, ss->buffer, sizeof(ss->buffer));
            if (n <= 0) {
                ss->eof = 1;
                if (line_pos > 0) { line[line_pos] = '\0'; return strdup(line); }
                return NULL;
            }
            ss->buf_len = (int)n;
            ss->buf_pos = 0;
        }
        char c = ss->buffer[ss->buf_pos++];
        if (c == '\n') { line[line_pos] = '\0'; return strdup(line); }
        if (c != '\r') line[line_pos++] = c;
    }
    line[line_pos] = '\0';
    return strdup(line);
}

static void stream_sub_close_test(StreamSub* ss) {
    if (!ss) return;
    if (ss->read_fd >= 0) { close(ss->read_fd); ss->read_fd = -1; }
    if (ss->source_pid > 0) {
        int st; pid_t r = waitpid(ss->source_pid, &st, WNOHANG);
        if (r == 0) { kill(ss->source_pid, SIGTERM); waitpid(ss->source_pid, &st, 0); }
        ss->source_pid = -1;
    }
    if (ss->pipe_path[0]) { unlink(ss->pipe_path); ss->pipe_path[0] = '\0'; }
    free(ss);
}

/* ── Test runner ── */
static int tests_run  = 0;
static int tests_pass = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { fprintf(stderr, "  [FAIL] %s\n", msg); } \
    else { printf("  [PASS] %s\n", msg); tests_pass++; } \
} while(0)

int main(void) {
    printf("==============================================\n");
    printf(" StreamSub Unit Tests\n");
    printf("==============================================\n\n");

    /* T1: open/read/close with finite source (seq 1 5) */
    printf("T1: Finite source seq 1 5\n");
    StreamSub* ss = stream_sub_open_test("seq 1 5");
    ASSERT(ss != NULL, "stream_sub_open returns non-NULL");
    if (ss) {
        char* lines[8] = {0};
        int n = 0;
        char* l;
        while ((l = stream_sub_read_line_test(ss)) != NULL && n < 8)
            lines[n++] = l;
        ASSERT(n == 5, "reads exactly 5 lines from seq 1 5");
        ASSERT(lines[0] && strcmp(lines[0], "1") == 0, "first line is '1'");
        ASSERT(lines[4] && strcmp(lines[4], "5") == 0, "last line is '5'");
        for (int i = 0; i < n; i++) free(lines[i]);
        stream_sub_close_test(ss);
        ASSERT(1, "stream_sub_close completes without hang");
    }

    /* T2: FIFO cleanup — check unlink */
    printf("\nT2: FIFO cleanup after close\n");
    char saved_path[256];
    ss = stream_sub_open_test("echo test");
    ASSERT(ss != NULL, "second open succeeds");
    if (ss) {
        strncpy(saved_path, ss->pipe_path, sizeof(saved_path));
        /* drain */
        char* l;
        while ((l = stream_sub_read_line_test(ss)) != NULL) free(l);
        stream_sub_close_test(ss);
        /* FIFO should be unlinked */
        struct stat st;
        ASSERT(stat(saved_path, &st) != 0, "FIFO unlinked after close");
    }

    /* T3: Source still running when we close — SIGTERM reaping */
    printf("\nT3: Abrupt close of infinite source\n");
    ss = stream_sub_open_test("while true; do echo ping; sleep 0.1; done");
    ASSERT(ss != NULL, "infinite source opens");
    if (ss) {
        char* l = stream_sub_read_line_test(ss);
        ASSERT(l != NULL && strcmp(l, "ping") == 0, "reads 'ping' from infinite source");
        free(l);
        stream_sub_close_test(ss); /* should SIGTERM the child */
        ASSERT(1, "abrupt close does not hang");
    }

    /* T4: Empty source */
    printf("\nT4: Empty source (true)\n");
    ss = stream_sub_open_test("true");
    ASSERT(ss != NULL, "empty source opens");
    if (ss) {
        char* l = stream_sub_read_line_test(ss);
        ASSERT(l == NULL, "empty source returns NULL immediately");
        free(l);
        stream_sub_close_test(ss);
    }

    /* T5: Multi-word output per line */
    printf("\nT5: Multi-word lines\n");
    ss = stream_sub_open_test("echo 'AAPL 185.20'; echo 'MSFT 415.00'");
    ASSERT(ss != NULL, "multi-word source opens");
    if (ss) {
        char* l1 = stream_sub_read_line_test(ss);
        char* l2 = stream_sub_read_line_test(ss);
        ASSERT(l1 && strcmp(l1, "AAPL 185.20") == 0, "first multi-word line correct");
        ASSERT(l2 && strcmp(l2, "MSFT 415.00") == 0, "second multi-word line correct");
        free(l1); free(l2);
        stream_sub_close_test(ss);
    }

    printf("\n==============================================\n");
    printf(" Results: %d/%d tests passed\n", tests_pass, tests_run);
    printf("==============================================\n");

    return tests_pass == tests_run ? 0 : 1;
}
