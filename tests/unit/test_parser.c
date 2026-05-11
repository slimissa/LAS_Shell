/*
 * test_parser.c
 * Tests find_streaming_substitution() and detect_streaming_assignment()
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>

/* Minimal stubs */
char* my_strdup(const char* s) { return strdup(s); }

/* ── Inline find_streaming_substitution from streaming_sub.c ── */
static char* find_streaming_substitution(const char* input, int* start_pos, int* end_pos) {
    *start_pos = -1; *end_pos = -1;
    if (!input) return NULL;
    int len = (int)strlen(input);
    int in_sq = 0, in_dq = 0;
    for (int i = 0; i < len; i++) {
        if (input[i] == '\'' && !in_dq) { in_sq = !in_sq; continue; }
        if (input[i] == '"'  && !in_sq) { in_dq = !in_dq; continue; }
        if (in_sq) continue;
        if (input[i] == '$' && i+2 < len && input[i+1] == '<' && input[i+2] == '(') {
            *start_pos = i;
            int depth = 1, j = i + 3;
            while (j < len && depth > 0) {
                if (input[j] == '(') depth++;
                else if (input[j] == ')') depth--;
                j++;
            }
            if (depth == 0) {
                *end_pos = j - 1;
                int cmd_len = j - i - 4;
                if (cmd_len <= 0) return NULL;
                char* cmd = malloc(cmd_len + 1);
                strncpy(cmd, input + i + 3, cmd_len);
                cmd[cmd_len] = '\0';
                return cmd;
            }
        }
    }
    return NULL;
}

/* ── Inline detect_streaming_assignment ── */
static int detect_streaming_assignment(const char* cond, char** var_name, char** stream_cmd) {
    const char* eq = strchr(cond, '=');
    if (!eq) return 0;
    const char* ae = eq + 1;
    if (ae[0] != '$' || ae[1] != '<' || ae[2] != '(') return 0;
    size_t nl = (size_t)(eq - cond);
    *var_name = malloc(nl + 1);
    strncpy(*var_name, cond, nl); (*var_name)[nl] = '\0';
    const char* cs = ae + 3;
    int d = 1; const char* p = cs;
    while (*p && d > 0) { if (*p == '(') d++; else if (*p == ')') d--; if (d>0) p++; }
    size_t cl = (size_t)(p - cs);
    *stream_cmd = malloc(cl + 1);
    strncpy(*stream_cmd, cs, cl); (*stream_cmd)[cl] = '\0';
    return 1;
}

static int tests_run = 0, tests_pass = 0;
#define ASSERT(cond, msg) do { tests_run++; \
    if (!(cond)) fprintf(stderr, "  [FAIL] %s\n", msg); \
    else { printf("  [PASS] %s\n", msg); tests_pass++; } } while(0)

int main(void) {
    printf("==============================================\n");
    printf(" Streaming Substitution Parser Tests\n");
    printf("==============================================\n\n");

    int s, e; char* cmd;

    /* P1: basic pattern */
    printf("P1: Basic $<(quote AAPL)\n");
    cmd = find_streaming_substitution("price=$<(quote AAPL)", &s, &e);
    ASSERT(cmd != NULL, "finds $<()");
    ASSERT(cmd && strcmp(cmd, "quote AAPL") == 0, "extracts 'quote AAPL'");
    ASSERT(s == 6,  "start pos is 6 (after 'price=')");
    ASSERT(e == 19, "end pos is 19");
    free(cmd);

    /* P2: no match */
    printf("\nP2: Plain $(cmd) — not $<()\n");
    cmd = find_streaming_substitution("x=$(echo hi)", &s, &e);
    ASSERT(cmd == NULL, "plain $() returns NULL");
    ASSERT(s == -1, "start_pos is -1");

    /* P3: inside single quotes — should NOT match */
    printf("\nP3: Inside single quotes\n");
    cmd = find_streaming_substitution("echo '$<(quote AAPL)'", &s, &e);
    ASSERT(cmd == NULL, "$<() inside single quotes is not expanded");

    /* P4: nested parens */
    printf("\nP4: Nested parens $<(echo $(date))\n");
    cmd = find_streaming_substitution("x=$<(echo $(date))", &s, &e);
    ASSERT(cmd != NULL, "finds outer $<()");
    ASSERT(cmd && strcmp(cmd, "echo $(date)") == 0, "inner $() preserved in cmd string");
    free(cmd);

    /* P5: detect_streaming_assignment */
    printf("\nP5: detect_streaming_assignment with 'price=$<(quote AAPL)'\n");
    char* vn = NULL, *sc = NULL;
    int is = detect_streaming_assignment("price=$<(quote AAPL)", &vn, &sc);
    ASSERT(is == 1,  "detected as streaming assignment");
    ASSERT(vn && strcmp(vn, "price") == 0, "var_name = 'price'");
    ASSERT(sc && strcmp(sc, "quote AAPL") == 0, "stream_cmd = 'quote AAPL'");
    free(vn); free(sc);

    /* P6: not a streaming assignment */
    printf("\nP6: detect_streaming_assignment with 'while true'\n");
    is = detect_streaming_assignment("true", &vn, &sc);
    ASSERT(is == 0, "'true' is not a streaming assignment");

    /* P7: regular $(cmd) not detected by streaming parser */
    printf("\nP7: regular $() not matched\n");
    is = detect_streaming_assignment("x=$(echo hi)", &vn, &sc);
    ASSERT(is == 0, "regular $() not a streaming assignment");

    printf("\n==============================================\n");
    printf(" Results: %d/%d tests passed\n", tests_pass, tests_run);
    printf("==============================================\n");
    return tests_pass == tests_run ? 0 : 1;
}
