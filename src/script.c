#include "../include/my_own_shell.h"

/* ══════════════════════════════════════════════════════════════════════════
 * WHILE / DO / DONE  Block Interpreter  (supports $<() streaming subs)
 * ══════════════════════════════════════════════════════════════════════════
 */

typedef struct { char** lines; int count; int cap; } LineList;

static void ll_init(LineList* ll) {
    ll->cap = 16; ll->count = 0;
    ll->lines = malloc(ll->cap * sizeof(char*));
}
static void ll_push(LineList* ll, const char* line) {
    if (ll->count >= ll->cap) {
        ll->cap *= 2;
        ll->lines = realloc(ll->lines, ll->cap * sizeof(char*));
    }
    ll->lines[ll->count++] = my_strdup(line);
}
static void ll_free(LineList* ll) {
    for (int i = 0; i < ll->count; i++) free(ll->lines[i]);
    free(ll->lines); ll->count = 0; ll->cap = 0; ll->lines = NULL;
}

static char* trim_ws(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
        *end-- = '\0';
    return s;
}

/* collect_block: reads from FILE* until matching "done", tracking nesting */
static int collect_block(FILE* f, LineList* body_lines) {
    char line[4096];
    int depth = 1;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
        char* _dup1 = my_strdup(line); char* trimmed = trim_ws(_dup1);
        if (strncmp(trimmed, "while ", 6) == 0 || strcmp(trimmed, "while") == 0)
            depth++;
        if (strcmp(trimmed, "done") == 0) {
            depth--;
            if (depth == 0) { free(_dup1); return 0; }
        }
        free(_dup1);
        ll_push(body_lines, line);
    }
    fprintf(stderr, "while: syntax error: missing 'done'\n");
    return -1;
}

static char* parse_while_condition(const char* line) {
    const char* p = line + 5; /* skip "while" */
    while (*p == ' ' || *p == '\t') p++;
    char* cond = my_strdup(p);
    char* semi = strstr(cond, ";");
    if (semi) *semi = '\0';
    char* end = cond + strlen(cond) - 1;
    while (end > cond && (*end == ' ' || *end == '\t')) *end-- = '\0';
    return cond;
}

static int detect_streaming_assignment(const char* cond, char** var_name, char** stream_cmd) {
    const char* eq = strchr(cond, '=');
    if (!eq) return 0;
    const char* after_eq = eq + 1;
    if (after_eq[0] != '$' || after_eq[1] != '<' || after_eq[2] != '(') return 0;
    size_t name_len = (size_t)(eq - cond);
    *var_name = malloc(name_len + 1);
    if (!*var_name) return 0;
    strncpy(*var_name, cond, name_len);
    (*var_name)[name_len] = '\0';
    const char* cmd_start = after_eq + 3;
    int depth = 1;
    const char* p = cmd_start;
    while (*p && depth > 0) {
        if (*p == '(') depth++;
        else if (*p == ')') depth--;
        if (depth > 0) p++;
    }
    size_t cmd_len = (size_t)(p - cmd_start);
    *stream_cmd = malloc(cmd_len + 1);
    if (!*stream_cmd) { free(*var_name); return 0; }
    strncpy(*stream_cmd, cmd_start, cmd_len);
    (*stream_cmd)[cmd_len] = '\0';
    return 1;
}

static int execute_while_block(const char* condition, LineList* body, char*** env_ptr) {
    char** env = *env_ptr;
    char* var_name = NULL;
    char* stream_cmd = NULL;
    int is_streaming = detect_streaming_assignment(condition, &var_name, &stream_cmd);
    StreamSub* ss = NULL;

    if (is_streaming) {
        fprintf(stderr, "[stream] opening $<(%s) → $%s\n", stream_cmd, var_name);
        ss = stream_sub_open(stream_cmd, env);
        if (!ss) {
            fprintf(stderr, "while: $<(%s): failed to open stream\n", stream_cmd);
            free(var_name); free(stream_cmd);
            return 1;
        }
    }

    int last_status = 0;
    int max_iter = 1000000;
    int do_break = 0;

    while (!get_watch_stop() && max_iter-- > 0 && !do_break) {
        if (is_streaming) {
            char* line_val = stream_sub_read_line(ss);
            if (!line_val) break; /* stream exhausted */
            /* Build: setenv VAR VALUE */
            size_t cmd_sz = strlen(var_name) + strlen(line_val) + 16;
            char* setenv_cmd = malloc(cmd_sz);
            snprintf(setenv_cmd, cmd_sz, "setenv %s %s", var_name, line_val);
            free(line_val);
            execute_command_line_env(setenv_cmd, env_ptr);
            env = *env_ptr;
            free(setenv_cmd);
        } else {
            char* expanded = expand_vars_in_line(condition, env);
            char* subs = process_line_with_substitutions(expanded);
            free(expanded);
            int cs = execute_command_line_env(subs, env_ptr);
            env = *env_ptr;
            free(subs);
            if (cs != 0) break;
        }

        for (int i = 0; i < body->count && !get_watch_stop() && !do_break; i++) {
            char* raw = my_strdup(body->lines[i]);
            char* trimmed = trim_ws(raw);
            if (trimmed[0] == '\0' || trimmed[0] == '#') { free(raw); continue; }
            if (strcmp(trimmed, "break") == 0)    { free(raw); do_break = 1; break; }
            if (strcmp(trimmed, "continue") == 0) { free(raw); break; }
            char* aliased  = expand_aliases(trimmed); free(raw);
            /* Expand $VAR with the CURRENT env before $() substitution so that
             * counter patterns like $(expr $I + 1) see the updated value of $I. */
            char* var_expanded = expand_vars_in_line(aliased, env); free(aliased);
            char* with_s   = process_line_with_substitutions(var_expanded); free(var_expanded);
            if (!with_s || with_s[0] == '\0') { free(with_s); continue; }
            last_status = execute_command_line_env(with_s, env_ptr);
            env = *env_ptr;
            free(with_s);
        }
    }

    if (ss) stream_sub_close(ss);
    free(var_name); free(stream_cmd);
    return last_status;
}

/* Minimal if/then/else/fi interpreter */
static int execute_if_block(const char* condition, LineList* then_body,
                             LineList* else_body, char*** env_ptr) {
    char** env = *env_ptr;
    char* expanded  = expand_vars_in_line(condition, env);
    char* with_subs = process_line_with_substitutions(expanded);
    free(expanded);
    int cs = execute_command_line_env(with_subs, env_ptr);
    env = *env_ptr; free(with_subs);
    LineList* body = (cs == 0) ? then_body : else_body;
    int last_status = 0;
    if (!body) return 0;
    for (int i = 0; i < body->count && !get_watch_stop(); i++) {
        char* raw = my_strdup(body->lines[i]);
        char* trimmed = trim_ws(raw);
        if (trimmed[0] == '\0' || trimmed[0] == '#') { free(raw); continue; }
        char* aliased  = expand_aliases(trimmed); free(raw);
        char* with_s   = process_line_with_substitutions(aliased); free(aliased);
        if (!with_s || with_s[0] == '\0') { free(with_s); continue; }
        last_status = execute_command_line_env(with_s, env_ptr);
        env = *env_ptr; free(with_s);
    }
    return last_status;
}

/* ── End block interpreter ── */

/* Expand ~/path to $HOME/path. Returns malloc'd string. */
static char* expand_tilde(const char* path) {
    if (!path) return NULL;
    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
        char* home = getenv("HOME");
        if (home) {
            size_t len = strlen(home) + strlen(path + 1) + 1;
            char* r = malloc(len);
            if (r) { snprintf(r, len, "%s%s", home, path + 1); return r; }
        }
    }
    return my_strdup(path);
}

/*
 * execute_command_line_env()
 *
 * Core executor. Takes char*** env_ptr so setenv/unsetenv changes
 * propagate back to the caller (critical for script line-by-line execution).
 * Also handles history, setenv, unsetenv which were missing here.
 */
int execute_command_line_env(char* input, char*** env_ptr) {
    char** env = *env_ptr;

    /* ── Phase 4.1: Per-line audit logging ──────────────────────────────
     *
     * When audit mode is active every line that reaches this function is
     * individually logged with its stdout hash and chain hash.
     *
     * We call audit_log_command() which:
     *   1. Captures stdout via a pipe
     *   2. Executes the command through execute_command_line() (NOT
     *      recursively through execute_command_line_env — that would
     *      double-log)
     *   3. Writes a chained CSV record to ~/.las_shell_audit
     *   4. Echoes captured stdout back to real stdout
     *
     * Recursion guard: audit_log_command() calls execute_command_line()
     * which calls execute_command_line_env() again, but audit_is_enabled()
     * still returns 1.  To prevent infinite recursion we use a per-thread
     * re-entrancy flag.  Static is safe because Las_shell is single-threaded.
     * ────────────────────────────────────────────────────────────────── */
    if (audit_is_enabled()) {
        static int in_audit_dispatch = 0;
        if (!in_audit_dispatch) {
            in_audit_dispatch = 1;
            int rc = audit_log_command(input, env);
            in_audit_dispatch = 0;
            return rc;
        }
        /* If we are inside audit_log_command already, fall through to
         * the normal execution path below (no double-logging).        */
    }

    /* ===== EXPAND $VAR IN RAW LINE before operator detection ===== */
    {
        char* expanded_line = expand_vars_in_line(input, env);
        if (expanded_line && expanded_line != input) {
            input = expanded_line; /* caller owns the original; we use expanded */
        }
    }

    /* ===== OPERATOR DETECTION — single | is NOT an operator ===== */
    int has_operator = 0;
    for (int i = 0; input[i]; i++) {
        char c  = input[i];
        char c1 = input[i + 1];
        if (c == ';') { has_operator = 1; break; }
        if (c == '&') { has_operator = 1; break; }
        if (c == '|' && c1 == '|') { has_operator = 1; break; }
        if (c == '|' && c1 == '>') { has_operator = 1; break; }
        if (c == '?' && c1 == '>') { has_operator = 1; break; }
        if (c == '~' && c1 == '>') { has_operator = 1; break; }
    }
    if (has_operator) {
        CommandNode* seq = parse_operators(input);
        if (seq) {
            int s = execute_sequence(seq, env_ptr);
            free_sequence(seq);
            return s;
        }
        return 0;
    }

    /* ===== PIPE DETECTION ===== */
    int has_pipe = 0;
    for (int i = 0; input[i]; i++) {
        if (input[i] == '|' && input[i + 1] != '|' && input[i + 1] != '>') { has_pipe = 1; break; }
    }
    if (has_pipe) {
        Pipeline* pl = parse_pipeline(input);
        if (pl) {
            int s = execute_pipeline(pl->commands, pl->cmd_count, env);
            free_pipeline(pl);
            return s;
        }
        return 0;
    }

    /* ===== NORMAL PARSING ===== */
    char** args = parse_input(input);
    if (!args || !args[0]) { if (args) free_tokens(args); return 0; }

    /* ── expand $VAR and ~ in all args ── */
    expand_args(args, env);

    int status = 0;

    /* history builtin — was missing, fell through to execvp */
    if (my_strcmp(args[0], "history") == 0) {
        HIST_ENTRY** hl = history_list();
        if (hl)
            for (int i = 0; hl[i]; i++)
                printf("%d  %s\n", i + 1, hl[i]->line);
        free_tokens(args);
        return 0;
    }

    /* setenv / unsetenv — propagate new env pointer back to caller */
    if (my_strcmp(args[0], "setenv") == 0) {
        env = command_setenv(args, env);
        *env_ptr = env;
        free_tokens(args);
        return 0;
    }
    if (my_strcmp(args[0], "unsetenv") == 0) {
        env = command_unsetenv(args, env);
        *env_ptr = env;
        free_tokens(args);
        return 0;
    }

    /* ── Las_shell trading built-ins ── */
    if (my_strcmp(args[0], "setmarket") == 0) {
        env = command_setmarket(args, env);
        *env_ptr = env; free_tokens(args); return 0;
    }
    if (my_strcmp(args[0], "setbroker") == 0) {
        env = command_setbroker(args, env);
        *env_ptr = env; free_tokens(args); return 0;
    }
    if (my_strcmp(args[0], "setaccount") == 0) {
        env = command_setaccount(args, env);
        *env_ptr = env; free_tokens(args); return 0;
    }
    if (my_strcmp(args[0], "setcapital") == 0) {
        env = command_setcapital(args, env);
        *env_ptr = env; free_tokens(args); return 0;
    }

    /* ── @time operator ── */
    if (args[0][0] == '@') {
        if (wait_until(args[0]) != 0) {
            fprintf(stderr, "@time: invalid format '%s'\n", args[0]);
            free_tokens(args); return 1;
        }
        if (!args[1]) { free_tokens(args); return 0; }
        char remaining[1024] = "";
        for (int i = 1; args[i]; i++) {
            if (i > 1) my_strncat(remaining, " ", sizeof(remaining)-my_strlen(remaining)-1);
            my_strncat(remaining, args[i], sizeof(remaining)-my_strlen(remaining)-1);
        }
        free_tokens(args);
        return execute_command_line_env(remaining, env_ptr);
    }

    /* ── assert: handle BEFORE redirection detection ──
     * '>' and '<' in "assert 5 > 3" must NOT be treated as redirections */
    if (my_strcmp(args[0], "assert") == 0) {
        int s = command_assert(args, env);
        free_tokens(args);
        return s;
    }

    /* ===== REDIRECTION DETECTION ===== */
    char* output_file   = NULL;
    char* input_file    = NULL;
    int   append_mode   = 0;
    int   has_out_redir = 0;
    int   has_in_redir  = 0;

    int arg_count = 0;
    for (int i = 0; args[i]; i++) {
        if (my_strcmp(args[i], ">") == 0 || my_strcmp(args[i], ">>") == 0 ||
            my_strcmp(args[i], "<") == 0)
            i++;
        else
            arg_count++;
    }

    char** clean_args = malloc((arg_count + 1) * sizeof(char*));
    if (!clean_args) { perror("malloc"); free_tokens(args); return 1; }

    int j = 0;
    for (int i = 0; args[i]; i++) {
        if (my_strcmp(args[i], ">") == 0) {
            has_out_redir = 1; append_mode = 0;
            if (args[i + 1]) { output_file = expand_tilde(args[i + 1]); }
            i++;
        } else if (my_strcmp(args[i], ">>") == 0) {
            has_out_redir = 1; append_mode = 1;
            if (args[i + 1]) { output_file = expand_tilde(args[i + 1]); }
            i++;
        } else if (my_strcmp(args[i], "<") == 0) {
            has_in_redir = 1;
            if (args[i + 1]) { input_file = expand_tilde(args[i + 1]); }
            i++;
        } else {
            clean_args[j++] = my_strdup(args[i]);
        }
    }
    clean_args[j] = NULL;

    if (has_in_redir || has_out_redir) {
        /* Try built-in first with redirected fds */
        int saved_stdout = -1, saved_stdin = -1;
        if (has_out_redir && output_file) {
            int fd = open(output_file, append_mode
                ? (O_WRONLY|O_CREAT|O_APPEND) : (O_WRONLY|O_CREAT|O_TRUNC), 0644);
            if (fd != -1) { saved_stdout = dup(STDOUT_FILENO); dup2(fd, STDOUT_FILENO); close(fd); }
        }
        if (has_in_redir && input_file) {
            int fd = open(input_file, O_RDONLY);
            if (fd != -1) { saved_stdin = dup(STDIN_FILENO); dup2(fd, STDIN_FILENO); close(fd); }
        }
        char* rdir = getcwd(NULL, 0);
        int b = shell_builtins(clean_args, env, rdir);
        free(rdir);
        if (b != -1) {
            fflush(stdout);   /* flush buffered printf output before fd restore */
            status = b;
        } else {
            if (saved_stdout != -1) { dup2(saved_stdout, STDOUT_FILENO); close(saved_stdout); saved_stdout = -1; }
            if (saved_stdin  != -1) { dup2(saved_stdin,  STDIN_FILENO);  close(saved_stdin);  saved_stdin  = -1; }
            if (has_in_redir && has_out_redir)
                status = execute_with_both_redirect(clean_args, env, input_file, output_file, append_mode);
            else if (has_in_redir)
                status = execute_with_input_redirect(clean_args, env, input_file);
            else
                status = execute_with_redirect(clean_args, env, output_file, append_mode);
        }
        if (saved_stdout != -1) { dup2(saved_stdout, STDOUT_FILENO); close(saved_stdout); }
        if (saved_stdin  != -1) { dup2(saved_stdin,  STDIN_FILENO);  close(saved_stdin);  }
    } else {
        char* initial_dir = getcwd(NULL, 0);
        int b = shell_builtins(args, env, initial_dir);
        if (b != -1) {
            status = b;
        } else {
            pid_t pid = fork();
            if (pid == 0) {
                execvp(args[0], args);
                fprintf(stderr, "Command not found: %s\n", args[0]);
                exit(127);
            } else if (pid > 0) {
                int ws;
                waitpid(pid, &ws, 0);
                status = WIFEXITED(ws) ? WEXITSTATUS(ws) : 1;
            } else {
                perror("fork");
                status = 1;
            }
        }
        free(initial_dir);
    }

    if (output_file) free(output_file);
    if (input_file)  free(input_file);
    for (int i = 0; clean_args[i]; i++) free(clean_args[i]);
    free(clean_args);
    free_tokens(args);
    return status;
}

/* Backward-compatible wrapper — used everywhere env doesn't need to propagate */
int execute_command_line(char* input, char** env) {
    return execute_command_line_env(input, &env);
}

/*
 * execute_script — reads a script file line by line.
 * Uses &env (double pointer) so setenv/unsetenv changes on one line
 * are visible on subsequent lines within the same script.
 *
 * Control structures handled:
 *   while COND; do / while COND \n do
 *   done
 *   if COND; then / if COND \n then
 *   else
 *   fi
 *
 *   $<(cmd) in the while condition is treated as a streaming assignment.
 */
int execute_script(char* filename, char** env) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "las-shell: %s: No such file or directory\n", filename);
        return 1;
    }

    struct stat st;
    if (stat(filename, &st) == -1) {
        fprintf(stderr, "las-shell: %s: Cannot access file\n", filename);
        fclose(f); return 1;
    }
    if (!(st.st_mode & S_IRUSR)) {
        fprintf(stderr, "las-shell: %s: Permission denied\n", filename);
        fclose(f); return 1;
    }

    char line[4096];
    int  exit_status = 0;

    /* ── State machine for multi-line control structures ── */
    /* We use a simple lookahead: when we see "while" we call collect_block(). */

    while (fgets(line, sizeof(line), f) != NULL) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') { line[--len] = '\0'; }
        if (len == 0) continue;

        /* Skip shebang line (#!) */
        if (len >= 2 && line[0] == '#' && line[1] == '!')
            continue;

        /* ── Backslash line continuation ──────────────────────────
         * If the line ends with \ (and \ is not inside quotes),
         * append the next line to form a single logical line.       */
        while (len > 0 && line[len-1] == '\\') {
            /* Check: is this \ inside quotes? Simple heuristic —
             * count unescaped quotes before the \.                   */
            int in_sq = 0, in_dq = 0;
            for (size_t qi = 0; qi < len - 1; qi++) {
                if (line[qi] == '\'' && !in_dq) in_sq = !in_sq;
                if (line[qi] == '"'  && !in_sq) in_dq = !in_dq;
            }
            if (in_sq || in_dq) break;  /* \ inside quotes — don't continue */

            /* Remove the trailing \ */
            line[--len] = '\0';

            /* Read the next line */
            char next_line[4096];
            if (fgets(next_line, sizeof(next_line), f) == NULL) break;
            size_t nlen = strlen(next_line);
            if (nlen > 0 && next_line[nlen-1] == '\n') next_line[--nlen] = '\0';

            /* Append to line buffer (truncate if too long) */
            size_t remain = sizeof(line) - len - 1;
            if (nlen > remain) nlen = remain;
            memcpy(line + len, next_line, nlen);
            len += nlen;
            line[len] = '\0';
        }

        char* _dup2 = my_strdup(line); char* trimmed = trim_ws(_dup2);


        /* Skip comments and blanks */
        if (trimmed[0] == '\0' || trimmed[0] == '#') { free(_dup2); continue; }

        /* ── while ── */
        if (strncmp(trimmed, "while ", 6) == 0) {
            char* condition = parse_while_condition(trimmed);
            free(_dup2);

            /* Consume the "do" line if it is on a separate line */
            /* (If the condition ends with "; do", do is already stripped) */
            /* Peek at next line — if it is "do" alone, skip it           */
            long pos_before_do = ftell(f);
            char do_line[4096];
            if (fgets(do_line, sizeof(do_line), f)) {
                char* _dup_do = my_strdup(do_line); char* do_trim = trim_ws(_dup_do);
                if (strcmp(do_trim, "do") != 0) {
                    /* Not a bare "do" — put it back by seeking */
                    fseek(f, pos_before_do, SEEK_SET);
                }
                free(_dup_do);
            }

            /* Collect body lines up to matching "done" */
            LineList body;
            ll_init(&body);
            int rc = collect_block(f, &body);
            if (rc != 0) {
                free(condition);
                ll_free(&body);
                fclose(f);
                return 1;
            }

            exit_status = execute_while_block(condition, &body, &env);
            free(condition);
            ll_free(&body);
            continue;
        }

        /* ── if ── */
        if (strncmp(trimmed, "if ", 3) == 0) {
            /* Extract condition: everything after "if " up to "; then" or EOL */
            char* p = trimmed + 3;
            char* condition = my_strdup(p);
            /* Strip trailing "; then" */
            char* semi = strstr(condition, ";");
            if (semi) *semi = '\0';
            char* end = condition + strlen(condition) - 1;
            while (end > condition && (*end == ' ' || *end == '\t')) *end-- = '\0';
            free(_dup2);

            /* Consume "then" line if separate */
            long pos_before_then = ftell(f);
            char then_line[4096];
            if (fgets(then_line, sizeof(then_line), f)) {
                char* _dup_then = my_strdup(then_line); char* then_trim = trim_ws(_dup_then);
                if (strcmp(then_trim, "then") != 0)
                    fseek(f, pos_before_then, SEEK_SET);
                free(_dup_then);
            }

            /* Collect then-body and optional else-body up to "fi" */
            LineList then_body, else_body;
            ll_init(&then_body); ll_init(&else_body);
            LineList* active = &then_body;
            int found_fi = 0;
            char if_line[4096];
            int depth = 1;

            while (fgets(if_line, sizeof(if_line), f)) {
                size_t il = strlen(if_line);
                if (il > 0 && if_line[il-1] == '\n') if_line[--il] = '\0';
                char* _dup_it = my_strdup(if_line); char* it = trim_ws(_dup_it);
                if (strncmp(it, "if ", 3) == 0) depth++;
                if (strcmp(it, "fi") == 0) {
                    depth--;
                    if (depth == 0) { found_fi = 1; free(_dup_it); break; }
                }
                if (depth == 1 && strcmp(it, "else") == 0) {
                    active = &else_body; free(_dup_it); continue;
                }
                free(_dup_it);
                ll_push(active, if_line);
            }

            if (!found_fi)
                fprintf(stderr, "if: syntax error: missing 'fi'\n");

            exit_status = execute_if_block(condition, &then_body,
                                           else_body.count > 0 ? &else_body : NULL,
                                           &env);
            free(condition);
            ll_free(&then_body); ll_free(&else_body);
            continue;
        }

        /* ── Normal line ── */
        char* expanded = expand_aliases(trimmed);
        free(_dup2);

        /* Expand $VAR with current env BEFORE $() substitution so that
         * patterns like $(expr $I + 1) see the live value of $I. */
        char* var_exp = expand_vars_in_line(expanded, env); free(expanded);
        char* with_subs = process_line_with_substitutions(var_exp);
        free(var_exp);
        if (!with_subs || with_subs[0] == '\0') { free(with_subs); continue; }

        add_to_history(line);

        exit_status = execute_command_line_env(with_subs, &env);
        update_exit_status(exit_status);
        free(with_subs);
    }

    fclose(f);
    return exit_status;
}