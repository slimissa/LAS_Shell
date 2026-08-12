#include "../include/my_own_shell.h"

/* ── @time operator helper ── */
int wait_until(const char* token) {
    int h, m, s;
    if (sscanf(token, "@%d:%d:%d", &h, &m, &s) != 3) return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) return -1;
    time_t now    = time(NULL);
    struct tm* lt = localtime(&now);
    int target    = h * 3600 + m * 60 + s;
    int now_secs  = lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec;
    int delta     = target - now_secs;
    if (delta < 0) delta += 86400;
    if (delta == 0) return 0;
    printf("@time: waiting %02d:%02d:%02d (%ds)...\n", h, m, s, delta);
    fflush(stdout);
    struct timespec ts = { 0, 100000000L };
    while (delta > 0 && !get_watch_stop()) {
        nanosleep(&ts, NULL);
        delta--;
        if (delta % 10 == 0) {
            now = time(NULL); lt = localtime(&now);
            now_secs = lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec;
            delta = target - now_secs;
            if (delta < 0) delta = 0;
        }
    }
    return 0;
}


Job jobs[MAX_JOBS];
int job_count = 0;
int next_job_id = 1;

void add_job(pid_t pid, char* command) {
    if (job_count < MAX_JOBS) {
        jobs[job_count].pid = pid;
        jobs[job_count].command = my_strdup(command);
        jobs[job_count].job_id = next_job_id++;
        jobs[job_count].active = 1;
        printf("[%d] %d\n", jobs[job_count].job_id, pid);
        job_count++;
    }
}

void clean_jobs() {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].active) {
            int status;
            pid_t result = waitpid(jobs[i].pid, &status, WNOHANG);
            if (result > 0) {
                jobs[i].active = 0;
                printf("[%d]+ Terminé\t%s\n", jobs[i].job_id, jobs[i].command);
                free(jobs[i].command);
            }
        }
    }
}

void print_jobs() {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].active) {
            printf("[%d] %d %s\n", jobs[i].job_id, jobs[i].pid, jobs[i].command);
        }
    }
}

// Découper sur les opérateurs
// FIX: ignore quotes, only match ||/&&/;/& — NOT single |
char** split_on_operators(char* input, int* count) {
    char** result = malloc(MAX_INPUT_SIZE * sizeof(char*));
    *count = 0;
    if (!result) return NULL;  /* FIX OP2 */

    char* input_copy = my_strdup(input);
    char* current = input_copy;
    char* start = current;

    /* track quote state so operators inside quotes are ignored */
    int  in_quotes  = 0;
    char quote_char = 0;
    /* FIX (assert || { compound }): track brace depth the same way quotes
     * are tracked, so a ';'/'&&'/'||'/etc. inside '{ ... }' is treated as
     * part of that single group operand, not as a new top-level split
     * point. Without this, '{ echo x; exit 1; }' got shredded into
     * independent top-level commands and 'exit 1' ran unconditionally. */
    int brace_depth = 0;
    /* FIX MS2 (regression fix): a bare | before any other operator has
     * been seen is part of the left-hand OPERAND (e.g. 'env | grep ~>
     * date' backtests the whole pipe as one unit) and must not split.
     * A bare | after an operator has already been emitted is a real
     * chain boundary (e.g. 'a ?> checker | next') and must split. */
    int seen_operator = 0;

    while (*current) {
        /* quote tracking */
        if (!in_quotes && (*current == '"' || *current == '\'')) {
            in_quotes  = 1;
            quote_char = *current;
            current++;
            continue;
        }
        if (in_quotes && *current == quote_char) {
            in_quotes  = 0;
            quote_char = 0;
            current++;
            continue;
        }
        if (in_quotes) { current++; continue; }

        /* brace tracking (only outside quotes) */
        if (*current == '{') { brace_depth++; current++; continue; }
        if (*current == '}') { if (brace_depth > 0) brace_depth--; current++; continue; }
        if (brace_depth > 0) { current++; continue; }

        /* operator detection — single | is NOT an operator here */
        int   op_len = 0;
        char  op_str[3] = {0};

        if (*current == '?' && *(current+1) == '>') {
            op_len = 2; op_str[0] = '?'; op_str[1] = '>';
        } else if (*current == '~' && *(current+1) == '>') {
            op_len = 2; op_str[0] = '~'; op_str[1] = '>';
        } else if (*current == '|' && *(current+1) == '>') {
            op_len = 2; op_str[0] = '|'; op_str[1] = '>';
        } else if (*current == '&' && *(current+1) == '&') {
            op_len = 2; op_str[0] = '&'; op_str[1] = '&';
        } else if (*current == '|' && *(current+1) == '|') {
            op_len = 2; op_str[0] = '|'; op_str[1] = '|';
        } else if (*current == ';') {
            op_len = 1; op_str[0] = ';';
        } else if (*current == '&' && *(current+1) != '&') {
            op_len = 1; op_str[0] = '&';
        } else if (seen_operator &&
                   *current == '|' && *(current+1) != '|' && *(current+1) != '>') {
            /* Only split here if we're past the left-hand operand of the
             * first operator in the line. */
            op_len = 1; op_str[0] = '|';
        }

        if (op_len > 0) {
            seen_operator = 1;
            /* save command before operator */
            if (current > start) {
                size_t cmd_len = (size_t)(current - start);
                char* cmd = malloc(cmd_len + 1);
                strncpy(cmd, start, cmd_len);
                cmd[cmd_len] = '\0';
                /* trim trailing spaces */
                char* end = cmd + strlen(cmd) - 1;
                while (end > cmd && (*end == ' ' || *end == '\t')) *end-- = '\0';
                if (strlen(cmd) > 0) result[(*count)++] = cmd;
                else free(cmd);
            }
            result[(*count)++] = my_strdup(op_str);
            current += op_len;
            start = current;
            continue;
        }
        current++;
    }

    /* last segment */
    if (current > start) {
        size_t cmd_len = (size_t)(current - start);
        char* cmd = malloc(cmd_len + 1);
        strncpy(cmd, start, cmd_len);
        cmd[cmd_len] = '\0';
        char* end = cmd + strlen(cmd) - 1;
        while (end > cmd && (*end == ' ' || *end == '\t')) *end-- = '\0';
        if (strlen(cmd) > 0) result[(*count)++] = cmd;
        else free(cmd);
    }

    result[*count] = NULL;
    free(input_copy);
    return result;
}

CommandNode* parse_operators(char* input) {
    CommandNode* head = NULL;
    CommandNode* current = NULL;
    
    int part_count;
    char** parts = split_on_operators(input, &part_count);
    if (!parts) return NULL;  /* FIX OP2 */
    
    for (int i = 0; i < part_count; i++) {
        if (strcmp(parts[i], "&&") == 0 || strcmp(parts[i], "||") == 0 || 
            strcmp(parts[i], "&") == 0  || strcmp(parts[i], ";") == 0  ||
            strcmp(parts[i], "|>") == 0 || strcmp(parts[i], "?>") == 0 ||
            strcmp(parts[i], "~>") == 0 || strcmp(parts[i], "|") == 0) {
            // C'est un opérateur, on l'ajoute au nœud précédent
            if (current) {
                current->operator = my_strdup(parts[i]);
            }
        } else {
            // C'est une commande
            CommandNode* node = malloc(sizeof(CommandNode));
            if (!node) break;  /* FIX OP1: OOM -- stop building the sequence here */
            memset(node, 0, sizeof(CommandNode));

            /* FIX (assert || { compound }): detect a brace-grouped operand.
             * split_on_operators() now keeps '{ ... }' intact as one part,
             * but doesn't strip the braces themselves. Skip leading
             * whitespace (the trim in split_on_operators only strips
             * trailing spaces) before checking. */
            char* trimmed = parts[i];
            while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
            size_t tlen = strlen(trimmed);
            if (tlen >= 2 && trimmed[0] == '{' && trimmed[tlen-1] == '}') {
                node->is_group  = 1;
                node->group_body = malloc(tlen - 1);
                memcpy(node->group_body, trimmed + 1, tlen - 2);
                node->group_body[tlen - 2] = '\0';
                node->args = malloc(sizeof(char*));
                node->args[0] = NULL;
            } else {
                // Parser la commande
                node->args = parse_input(parts[i]);
            }
            
            // Vérifier si la commande se termine par & (pour background)
            int last_arg = 0;
            while (node->args[last_arg] != NULL) last_arg++;
            if (last_arg > 0) {
                last_arg--;
                if (strcmp(node->args[last_arg], "&") == 0) {
                    node->background = 1;
                    // Enlever & des arguments
                    free(node->args[last_arg]);
                    node->args[last_arg] = NULL;
                }
            }
            
            // Ajouter à la liste
            if (head == NULL) {
                head = node;
                current = node;
            } else {
                current->next = node;
                current = node;
            }
        }
    }
    
    // Libérer parts
    for (int i = 0; i < part_count; i++) {
        free(parts[i]);
    }
    free(parts);
    
    return head;
}

/* ── ~> Backtest operator helpers ────────────────────────────────────────
 *
 * Date range formats supported:
 *   YYYY-MM-DD:YYYY-MM-DD   e.g. 2020-01-01:2023-12-31
 *   YYYY:YYYY               e.g. 2019:2024  (shorthand → Jan 1 – Dec 31)
 *
 * Returns 1 on success, 0 on parse failure.
 * Writes null-terminated start/end strings into the provided buffers.
 * ──────────────────────────────────────────────────────────────────────── */
static int parse_backtest_date(const char* date_range,
                                char* start_buf, size_t start_len,
                                char* end_buf,   size_t end_len) {
    if (!date_range) return 0;

    /* Find the separator colon that splits start:end.
     * We skip colons that appear inside a date (YYYY-MM-DD uses dashes,
     * not colons, so the only colon is the separator).               */
    const char* sep = strchr(date_range, ':');
    if (!sep) return 0;

    size_t left_len = (size_t)(sep - date_range);

    /* Shorthand: YYYY:YYYY  (4-char left side) */
    if (left_len == 4) {
        /* Validate left side: exactly 4 decimal digits */
        for (size_t i = 0; i < 4; i++)
            if (date_range[i] < '0' || date_range[i] > '9') return 0;
        /* Validate right side: must be EXACTLY 4 chars, all decimal digits */
        if (strlen(sep + 1) != 4) return 0;
        for (size_t i = 0; i < 4; i++)
            if (sep[1+i] < '0' || sep[1+i] > '9') return 0;
        snprintf(start_buf, start_len, "%.4s-01-01", date_range);
        snprintf(end_buf,   end_len,   "%.4s-12-31", sep + 1);
        return 1;
    }

    /* Full format: YYYY-MM-DD:YYYY-MM-DD  (10-char left, 10-char right) */
    if (left_len == 10) {
        size_t right_len = strlen(sep + 1);
        if (right_len != 10) return 0;

        /* ── FIX: validate digit/dash pattern ── */
        const char* patterns[2] = { date_range, sep + 1 };
        for (int p = 0; p < 2; p++) {
            const char* s = patterns[p];
            if (s[0] < '0' || s[0] > '9') return 0;  /* Y */
            if (s[1] < '0' || s[1] > '9') return 0;  /* Y */
            if (s[2] < '0' || s[2] > '9') return 0;  /* Y */
            if (s[3] < '0' || s[3] > '9') return 0;  /* Y */
            if (s[4] != '-')               return 0;  /* - */
            if (s[5] < '0' || s[5] > '9') return 0;  /* M */
            if (s[6] < '0' || s[6] > '9') return 0;  /* M */
            if (s[7] != '-')               return 0;  /* - */
            if (s[8] < '0' || s[8] > '9') return 0;  /* D */
            if (s[9] < '0' || s[9] > '9') return 0;  /* D */
        }
        snprintf(start_buf, start_len, "%.*s", (int)left_len, date_range);
        snprintf(end_buf,   end_len,   "%.*s", (int)right_len, sep + 1);
        return 1;
    }

    return 0;
}

/*
 * execute_backtest_op()
 *
 * Implements the ~> operator.
 *
 *   left_args  : the command/pipeline to execute in backtest mode
 *   date_range : the date string from the right side of ~>
 *   env        : current environment
 *   returns    : exit status of the left command
 *
 * Steps:
 *   1. Parse date range → BACKTEST_START, BACKTEST_END
 *   2. Inject BACKTEST_MODE=1, BACKTEST_START, BACKTEST_END into env
 *      by reusing command_setenv() — no new mechanism required
 *   3. Also call setenv() to sync POSIX env for Python child processes
 *   4. Execute left_args with the enriched env
 *   5. Restore BACKTEST_MODE=0 after execution
 */
static int execute_backtest_op(char** left_args, const char* date_range,
                                char*** env_ptr) {
    char start_date[32] = {0};
    char end_date[32]   = {0};

    if (!parse_backtest_date(date_range, start_date, sizeof(start_date),
                              end_date, sizeof(end_date))) {
        fprintf(stderr,
            "~>: invalid date range '%s'\n"
            "    formats: YYYY-MM-DD:YYYY-MM-DD  or  YYYY:YYYY\n",
            date_range ? date_range : "(null)");
        return 1;
    }

    printf("[~>] backtest mode: %s to %s\n", start_date, end_date);
    fflush(stdout);

    /* ── BUG FIX 1: use env_ptr (char***) so command_setenv changes propagate
     *   back to execute_sequence's env variable.
     *   command_setenv returns a new pointer; we must write it through env_ptr. ── */
    char* s1[] = { "setenv", "BACKTEST_MODE",  "1",          NULL };
    char* s2[] = { "setenv", "BACKTEST_START", start_date,   NULL };
    char* s3[] = { "setenv", "BACKTEST_END",   end_date,     NULL };

    *env_ptr = command_setenv(s1, *env_ptr);
    *env_ptr = command_setenv(s2, *env_ptr);
    *env_ptr = command_setenv(s3, *env_ptr);

    /* Also sync POSIX env so Python/child processes see these via os.environ */
    setenv("BACKTEST_MODE",  "1",        1);
    setenv("BACKTEST_START", start_date, 1);
    setenv("BACKTEST_END",   end_date,   1);

    /* ── Reconstruct the command line from left_args ── */
    char cmd_line[4096] = {0};
    size_t remaining = sizeof(cmd_line) - 1;
    for (int i = 0; left_args[i]; i++) {
        if (i > 0) {
            if (remaining < 1) break;
            my_strncat(cmd_line, " ", remaining);
            remaining--;
        }
        size_t arg_len = (size_t)my_strlen(left_args[i]);
        if (arg_len > remaining) arg_len = remaining;
        my_strncat(cmd_line, left_args[i], arg_len);
        remaining -= arg_len;
    }

    /* ── Execute left side with backtest env ── */
    int status = execute_command_line(cmd_line, *env_ptr);
    
    /* Flush stdout and add a newline to separate from any following operator output */
    fflush(stdout);
    fflush(stderr);

    /* ── After run: reset BACKTEST_MODE=0 but keep START/END for observability ── */
    char* r1[] = { "setenv", "BACKTEST_MODE", "0", NULL };
    *env_ptr = command_setenv(r1, *env_ptr);
    setenv("BACKTEST_MODE", "0", 1);

    return status;
}

int execute_sequence(CommandNode* head, char*** env_ptr) {
    CommandNode* node = head;
    CommandNode* prev = NULL;   /* FIX: track previous node to read its operator */
    int last_status = 0;
    char** env = *env_ptr;

    char* initial_dir = getcwd(NULL, 0);

    while (node != NULL) {
        if (!node->is_group && (node->args == NULL || node->args[0] == NULL)) {
            prev = node;
            node = node->next;
            continue;
        }

        if (prev != NULL && prev->operator != NULL) {
            if (strcmp(prev->operator, "&&") == 0 && last_status != 0) {
                prev = node; node = node->next; continue;
            }
            if (strcmp(prev->operator, "||") == 0 && last_status == 0) {
                prev = node; node = node->next; continue;
            }
            /* background handling moved below, keyed off this node's own
             * ->operator rather than the previous node's -- see the
             * FIX note further down before the dispatch. */
            if (strcmp(prev->operator, "|>") == 0) {
                if (node->args && node->args[0])
                    last_status = execute_with_csv_log(prev->args, env, node->args[0]);
                prev = node; node = node->next; continue;
            }
            if (strcmp(prev->operator, "?>") == 0) {
                /* FIX MS2: 'left ?> checker | next' — if the checker node
                 * is itself followed by a bare '|', consume that next
                 * command too and feed the passed data into it, instead
                 * of letting it fall through as an unrelated node (which
                 * previously got silently absorbed into checker's argv). */
                if (node->args && node->args[0] &&
                    node->operator && strcmp(node->operator, "|") == 0 &&
                    node->next && node->next->args && node->next->args[0]) {
                    CommandNode* next_node = node->next;
                    /* FIX (found while testing the templates): if 'next'
                     * is itself followed by '|> logfile', the naive
                     * approach runs next once here and then AGAIN when
                     * the |> handler processes it next iteration — a
                     * real double-execution, confirmed with a counter
                     * script run through '?> checker | script |> log'
                     * executing twice before this fix. Detect and
                     * absorb the |> here so 'next' runs exactly once. */
                    if (next_node->operator && strcmp(next_node->operator, "|>") == 0 &&
                        next_node->next && next_node->next->args && next_node->next->args[0]) {
                        last_status = execute_risk_gate_chained_logged(
                            prev->args, node->args, next_node->args,
                            next_node->next->args[0], env);
                        prev = next_node->next; node = next_node->next->next; continue;
                    }
                    last_status = execute_risk_gate_chained(prev->args, node->args,
                                                             next_node->args, env);
                    prev = next_node; node = next_node->next; continue;
                }
                if (node->args && node->args[0])
                    last_status = execute_risk_gate(prev->args, node->args, env);
                else last_status = 1;
                prev = node; node = node->next; continue;
            }
            if (strcmp(prev->operator, "|") == 0) {
                /* FIX MS2 (generic case): plain 'left | right' mixed into a
                 * line that also has other operators elsewhere (e.g.
                 * 'a | b && c'). This never reaches parse_pipeline's real
                 * pipe engine since has_operator routes the whole line
                 * through here. Run left and right connected by a real
                 * OS pipe, streaming — not limited by any capture buffer. */
                if (node->args && node->args[0]) {
                    int pfd[2];
                    if (pipe(pfd) == 0) {
                        pid_t lp = fork();
                        if (lp == 0) {
                            close(pfd[0]);
                            dup2(pfd[1], STDOUT_FILENO);
                            close(pfd[1]);
                            char lcmd[4096] = {0};
                            for (int i = 0; prev->args[i]; i++) {
                                if (i > 0) my_strncat(lcmd, " ", sizeof(lcmd) - my_strlen(lcmd) - 1);
                                my_strncat(lcmd, prev->args[i], sizeof(lcmd) - my_strlen(lcmd) - 1);
                            }
                            char* la[4] = { (char*)(self_exe_path() ? self_exe_path() : "./las_shell"), "-c", lcmd, NULL };
                            execvp(la[0], la);
                            char* lb[4] = { "/bin/sh", "-c", lcmd, NULL };
                            execvp("/bin/sh", lb);
                            exit(127);
                        }
                        pid_t rp = fork();
                        if (rp == 0) {
                            close(pfd[1]);
                            dup2(pfd[0], STDIN_FILENO);
                            close(pfd[0]);
                            char rcmd[4096] = {0};
                            for (int i = 0; node->args[i]; i++) {
                                if (i > 0) my_strncat(rcmd, " ", sizeof(rcmd) - my_strlen(rcmd) - 1);
                                my_strncat(rcmd, node->args[i], sizeof(rcmd) - my_strlen(rcmd) - 1);
                            }
                            char* ra[4] = { (char*)(self_exe_path() ? self_exe_path() : "./las_shell"), "-c", rcmd, NULL };
                            execvp(ra[0], ra);
                            char* rb[4] = { "/bin/sh", "-c", rcmd, NULL };
                            execvp("/bin/sh", rb);
                            exit(127);
                        }
                        close(pfd[0]); close(pfd[1]);
                        int lws, rws;
                        if (lp > 0) waitpid(lp, &lws, 0);
                        if (rp > 0) waitpid(rp, &rws, 0);
                        last_status = (rp > 0 && WIFEXITED(rws)) ? WEXITSTATUS(rws) : 1;
                    } else {
                        last_status = 1;
                    }
                }
                prev = node; node = node->next; continue;
            }
            if (strcmp(prev->operator, "~>") == 0) {
                /* date range is node->args[0] (right side of ~>) */
                const char* date_range = (node->args && node->args[0])
                                         ? node->args[0] : NULL;
                last_status = execute_backtest_op(prev->args, date_range, env_ptr);
                env = *env_ptr;
                /* Skip past the date argument so it doesn't get executed as a command */
                prev = node; node = node->next;
                continue;
            }
        }

        if (node->operator && strcmp(node->operator, "&") == 0) {
            node->background = 1;
        }

        if (node->is_group) {
            if (node->background) {
                pid_t pid = fork();
                if (pid == 0) {
                    int rc = execute_command_line_env(node->group_body, env_ptr);
                    exit(rc < 0 ? 1 : rc);
                } else if (pid > 0) {
                    add_job(pid, node->group_body);
                    last_status = 0;
                } else {
                    perror("fork");
                    last_status = 1;
                }
            } else {
                last_status = execute_command_line_env(node->group_body, env_ptr);
                env = *env_ptr;  /* the group may have run setenv/source/etc. */
            }
            prev = node;
            node = node->next;
            continue;
        }

        /* CRITICAL FIX (found while fixing MS2): if we reach here, none of
         * the prev->operator handlers above consumed this node specially.
         * If this node's OWN operator is one of the "capturing" operators
         * (|>, ?>, ~>, |), it must NOT be run by the general fallback below
         * -- it will be executed exactly once, with proper output capture/
         * redirection, by that operator's handler on the next iteration
         * (where it becomes 'prev'). Without this, e.g. a live order on the
         * left of '?>' fires for real here, unconditionally, before any
         * risk gate ever sees it -- verified with a real side-effect test
         * (a script appending to a counter file ran twice, for both ?> and
         * |>, before this fix), confirming this predates today's changes
         * and is systemic to the operator-chain engine, not new. */
        if (node->operator != NULL &&
            (strcmp(node->operator, "|>") == 0 ||
             strcmp(node->operator, "?>") == 0 ||
             strcmp(node->operator, "~>") == 0 ||
             strcmp(node->operator, "|")  == 0)) {
            prev = node;
            node = node->next;
            continue;
        }

        clean_jobs();

        if (node->background) {
            pid_t pid = fork();
            if (pid == 0) {
                int brc = shell_builtins(node->args, env_ptr, initial_dir);
                if (brc != -1) exit(brc < 0 ? 1 : brc);
                execvp(node->args[0], node->args);
                fprintf(stderr, "Command not found: %s\n", node->args[0]);
                exit(127);
            } else if (pid > 0) {
                char cmd_line[4096] = "";
                for (int i = 0; node->args[i] != NULL; i++) {
                    if (i > 0) my_strcat(cmd_line, " ");
                    my_strcat(cmd_line, node->args[i]);
                }
                add_job(pid, cmd_line);
                last_status = 0;
            } else {
                perror("fork");
                last_status = 1;
            }
        } else {
            /* ── expand aliases on each node ── */
            {
                char full_cmd[4096] = "";
                for (int ai = 0; node->args[ai]; ai++) {
                    if (ai > 0) my_strcat(full_cmd, " ");
                    my_strncat(full_cmd, node->args[ai], sizeof(full_cmd)-my_strlen(full_cmd)-1);
                }
                char* al_exp = expand_aliases(full_cmd);
                if (al_exp && my_strcmp(al_exp, full_cmd) != 0) {
                    last_status = execute_command_line(al_exp, env);
                    free(al_exp);
                    prev = node; node = node->next; continue;
                }
                if (al_exp) free(al_exp);
            }

            /* ── expand $VAR and ~ before dispatch ── */
            expand_args(node->args, env);

            /* ── @time operator ── */
            char** exec_args = node->args;
            if (exec_args[0] && exec_args[0][0] == '@') {
                if (wait_until(exec_args[0]) == 0) { exec_args++; }
                else {
                    fprintf(stderr, "@time: invalid format '%s'\n", exec_args[0]);
                    last_status = 1; prev = node; node = node->next; continue;
                }
                if (!exec_args || !exec_args[0]) { prev = node; node = node->next; continue; }
            }
            /* FIX BUG: try builtins first — execvp can never run cd/echo/etc */
            /* Also intercept env-mutating builtins so changes propagate to
             * subsequent nodes in the same sequence (e.g. setenv FOO bar; echo $FOO) */
            if (exec_args[0] && (
                    my_strcmp(exec_args[0], "setenv")     == 0 ||
                    my_strcmp(exec_args[0], "unsetenv")   == 0 ||
                    my_strcmp(exec_args[0], "setmarket")  == 0 ||
                    my_strcmp(exec_args[0], "setbroker")  == 0 ||
                    my_strcmp(exec_args[0], "setaccount") == 0 ||
                    my_strcmp(exec_args[0], "setcapital") == 0)) {
                /* Call the env-mutating builtin and propagate the new env ptr */
                if (my_strcmp(exec_args[0], "setenv") == 0)
                    *env_ptr = command_setenv(exec_args, *env_ptr);
                else if (my_strcmp(exec_args[0], "unsetenv") == 0)
                    *env_ptr = command_unsetenv(exec_args, *env_ptr);
                else if (my_strcmp(exec_args[0], "setmarket") == 0)
                    *env_ptr = command_setmarket(exec_args, *env_ptr);
                else if (my_strcmp(exec_args[0], "setbroker") == 0)
                    *env_ptr = command_setbroker(exec_args, *env_ptr);
                else if (my_strcmp(exec_args[0], "setaccount") == 0)
                    *env_ptr = command_setaccount(exec_args, *env_ptr);
                else if (my_strcmp(exec_args[0], "setcapital") == 0)
                    *env_ptr = command_setcapital(exec_args, *env_ptr);
                env = *env_ptr;  /* refresh local alias */
                last_status = 0;
                prev = node; node = node->next; continue;
            }
            int builtin_ret = shell_builtins(exec_args, env_ptr, initial_dir);
            env = *env_ptr;  /* refresh: shell_builtins may have run 'source', updating *env_ptr */
            if (builtin_ret != -1) {
                last_status = builtin_ret;
            } else {
                pid_t pid = fork();
                if (pid == 0) {
                    execvp(exec_args[0], exec_args);
                    fprintf(stderr, "Command not found: %s\n", exec_args[0]);
                    exit(127);
                } else if (pid > 0) {
                    int status;
                    waitpid(pid, &status, 0);
                    last_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
                } else {
                    perror("fork");
                    last_status = 1;
                }
            }
        }

        prev = node;
        node = node->next;
    }

    if (initial_dir) free(initial_dir);
    return last_status;
}

void free_sequence(CommandNode* head) {
    CommandNode* current = head;
    while (current != NULL) {
        CommandNode* next = current->next;
        if (current->args) free_tokens(current->args);
        if (current->operator) free(current->operator);
        if (current->group_body) free(current->group_body);
        if (current->input_file) free(current->input_file);
        if (current->output_file) free(current->output_file);
        free(current);
        current = next;
    }
}