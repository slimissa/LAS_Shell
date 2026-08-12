#include "../include/my_own_shell.h"
#include "../include/risk_config.h"

int execute_with_redirect(char** args, char** env, char* output_file, int append_mode) {
    (void)env;
    pid_t pid = fork();
    
    if (pid == 0) {
        // Processus enfant
        
        // Redirection de sortie (déjà existante)
        if (output_file != NULL) {
            int fd;
            /* FIX RD4: was 0644 (world-readable). This is a regulated
             * trading shell that may redirect order/P&L data into these
             * files; default to owner-only like every other file this
             * codebase writes (paper account, audit log, checkpoint). */
            if (append_mode) {
                fd = open(output_file, O_WRONLY | O_CREAT | O_APPEND, 0600);
            } else {
                fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            }
            
            if (fd == -1) {
                perror("open");
                exit(1);
            }
            
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        
        execvp(args[0], args);
        fprintf(stderr, "Command not found: %s\n", args[0]);
        exit(1);
    }
    else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return status;
    }
    else {
        perror("fork");
        return -1;
    }
}

// NOUVELLE FONCTION : Redirection d'entrée
int execute_with_input_redirect(char** args, char** env, char* input_file) {
    (void)env;
    pid_t pid = fork();
    
    if (pid == 0) {
        // Processus enfant
        
        // Redirection d'entrée depuis le fichier
        if (input_file != NULL) {
            int fd = open(input_file, O_RDONLY);
            if (fd == -1) {
                perror("open");
                exit(1);
            }
            
            // Rediriger stdin vers le fichier
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        
        execvp(args[0], args);
        fprintf(stderr, "Command not found: %s\n", args[0]);
        exit(1);
    }
    else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return status;
    }
    else {
        perror("fork");
        return -1;
    }
}

// Fonction combinée pour gérer les deux redirections
int execute_with_both_redirect(char** args, char** env,
                               char* input_file, 
                               char* output_file, 
                               int append_mode) {
    (void)env;
    pid_t pid = fork();
    
    if (pid == 0) {
        // Processus enfant
        
        // Redirection d'entrée (si spécifiée)
        if (input_file != NULL) {
            int fd_in = open(input_file, O_RDONLY);
            if (fd_in == -1) {
                perror("open input");
                exit(1);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }
        
        // Redirection de sortie (si spécifiée)
        if (output_file != NULL) {
            int fd_out;
            if (append_mode) {
                fd_out = open(output_file, O_WRONLY | O_CREAT | O_APPEND, 0600);
            } else {
                fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            }
            if (fd_out == -1) {
                perror("open output");
                exit(1);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }
        
        execvp(args[0], args);
        fprintf(stderr, "Command not found: %s\n", args[0]);
        exit(1);
    }
    else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return status;
    }
    else {
        perror("fork");
        return -1;
    }
}
/* FIX RD7: shared quote-escaping helper, factored out of
 * execute_risk_gate_chained's already-correct logic. Appends arg to
 * dest, single-quoting it (with '\'' escaping for embedded single
 * quotes) if it contains anything a shell tokenizer would otherwise
 * split on, so re-joined args round-trip through 'las_shell -c'
 * exactly as they were originally tokenized. */
static void append_shell_quoted(char *dest, size_t dest_size, const char *arg) {
    int needs_quote = (strchr(arg, ' ') || strchr(arg, '\t') ||
                       strchr(arg, '\\') || strchr(arg, '"') ||
                       strchr(arg, '\'') || strchr(arg, '\n'));
    size_t c = strlen(dest);
    size_t rem = dest_size - c - 1;
    if (needs_quote && rem > 2) {
        dest[c++] = '\'';
        for (const char *p = arg; *p && c < dest_size - 2; p++) {
            if (*p == '\'') {
                if (c + 4 < dest_size - 1) {
                    dest[c++] = '\'';
                    dest[c++] = '\\';
                    dest[c++] = '\'';
                    dest[c++] = '\'';
                }
            } else {
                dest[c++] = *p;
            }
        }
        dest[c++] = '\'';
        dest[c] = '\0';
    } else {
        strncat(dest, arg, rem);
    }
}

/* ── execute_with_csv_log() ────────────────────────────────────────── */
int execute_with_csv_log(char** args, char** env, const char* csv_file) {
    (void)env;
    if (!args || !args[0] || !csv_file) return 1;

    /* Reconstruct the full command string so we can run it via
     * 'las_shell -c <cmd>'.  This is ESSENTIAL for broker builtins
     * (order, positions, balance, etc.) which only exist inside the
     * shell process — bare execvp("order",...) would fail with
     * "Command not found".  By routing through las_shell -c we let
     * the full builtin dispatch table handle the command.
     *
     * FIX RD7: previously rejoined args with a plain space, no
     * requoting -- any single arg that originally contained a space
     * (e.g. a quoted string the shell had already correctly tokenized
     * as one argument) had its boundary silently corrupted when
     * re-parsed by 'las_shell -c'. Use the same quoting helper the
     * ?> gate already uses correctly. */
    char cmd_line[4096] = {0};
    for (int i = 0; args[i]; i++) {
        if (i > 0) {
            size_t cur = strlen(cmd_line);
            if (cur + 1 < sizeof(cmd_line) - 1)
                cmd_line[cur] = ' ';
        }
        append_shell_quoted(cmd_line, sizeof(cmd_line), args[i]);
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) { perror("pipe"); return 1; }
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        /* Try las_shell first (handles all built-ins including broker),
         * fall back to /bin/sh for pure external commands.            */
        char* las_args[4] = { (char*)(self_exe_path() ? self_exe_path() : "./las_shell"), "-c", cmd_line, NULL };
        execvp(las_args[0], las_args);
        char* sh_args[4]  = { "/bin/sh",    "-c", cmd_line, NULL };
        execvp("/bin/sh", sh_args);
        fprintf(stderr, "execute_with_csv_log: exec failed for: %s\n", cmd_line);
        exit(127);
    }
    if (pid < 0) { perror("fork"); close(pipefd[0]); close(pipefd[1]); return 1; }
    close(pipefd[1]);
    FILE* log = fopen(csv_file, "a");
    if (!log) { perror("fopen"); close(pipefd[0]); waitpid(pid, NULL, 0); return 1; }
    FILE* pipe_read = fdopen(pipefd[0], "r");
    if (!pipe_read) { perror("fdopen"); fclose(log); waitpid(pid, NULL, 0); return 1; }
    char line[4096];
    while (fgets(line, sizeof(line), pipe_read)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        time_t now = time(NULL);
        struct tm* lt = localtime(&now);
        char ts[32];
        my_strftime(ts, sizeof(ts), "%Y-%m-%dT%T", lt);
        fprintf(log, "%s,%s\n", ts, line);
    }
    fclose(pipe_read);
    fclose(log);
    int ws;
    waitpid(pid, &ws, 0);
    return WIFEXITED(ws) ? WEXITSTATUS(ws) : 1;
}

/* ── execute_risk_gate() ────────────────────────────────────────────
 * left_args ?> right_args
 * 1. Run left, capture all stdout into buffer.
 * 2. Feed buffer into right's stdin.
 * 3. right exit 0  → return 0 (pass).
 *    right exit !=0 → log to ~/.las_shell_risk_rejections, return 1.
 *
 * NOTE: Both left and right sides are run via 'las_shell -c <cmd>'
 * so that Las_shell builtins (order, positions, etc.) work correctly
 * as left operands.
 * ─────────────────────────────────────────────────────────────── */
int execute_risk_gate(char** left_args, char** right_args, char** env) {
    return execute_risk_gate_chained(left_args, right_args, NULL, env);
}

/* execute_risk_gate_chained_logged() -- handles the common real-world
 * pattern 'left ?> checker | next |> logfile' as a single execution of
 * `next`, writing its output to both the terminal and a timestamped CSV
 * line (matching execute_with_csv_log's format). Without this, the
 * generic |> handler would run `next` a SECOND time after this function
 * already ran it once as part of the risk gate's chain -- confirmed via
 * a real side-effect test (a counter-incrementing script run through
 * '?> checker | script |> log' executed twice before this fix). */
int execute_risk_gate_chained_logged(char** left_args, char** right_args,
                                      char** next_args, const char* csv_file,
                                      char** env) {
    (void)env;
    if (!left_args || !left_args[0] || !right_args || !right_args[0]) return 1;
    if (!next_args || !next_args[0] || !csv_file) return 1;

    /* Steps 1-2 (capture left, gate through checker) are identical to
     * execute_risk_gate_chained; duplicated here to keep the PASS branch
     * self-contained for the dual-output next-stage execution below. */
    int left_pipe[2];
    if (pipe(left_pipe) == -1) { perror("pipe left"); return 1; }
    pid_t left_pid = fork();
    if (left_pid == 0) {
        close(left_pipe[0]);
        dup2(left_pipe[1], STDOUT_FILENO);
        close(left_pipe[1]);
        char left_cmd[4096] = {0};
        for (int i = 0; left_args[i]; i++) {
            if (i > 0) my_strncat(left_cmd, " ", sizeof(left_cmd) - my_strlen(left_cmd) - 1);
            my_strncat(left_cmd, left_args[i], sizeof(left_cmd) - my_strlen(left_cmd) - 1);
        }
        char* la[4] = { (char*)(self_exe_path() ? self_exe_path() : "./las_shell"), "-c", left_cmd, NULL };
        execvp(la[0], la);
        char* lb[4] = { "/bin/sh", "-c", left_cmd, NULL };
        execvp("/bin/sh", lb);
        exit(127);
    }
    if (left_pid < 0) { perror("fork left"); close(left_pipe[0]); close(left_pipe[1]); return 1; }
    close(left_pipe[1]);
    char* captured = malloc(65536);
    if (!captured) { close(left_pipe[0]); waitpid(left_pid, NULL, 0); return 1; }
    captured[0] = '\0';
    size_t cap_len = 0;
    char rbuf[4096];
    ssize_t nr;
    while ((nr = read(left_pipe[0], rbuf, sizeof(rbuf))) > 0) {
        if (cap_len + (size_t)nr < 65535) {
            memcpy(captured + cap_len, rbuf, (size_t)nr);
            cap_len += (size_t)nr;
            captured[cap_len] = '\0';
        }
    }
    close(left_pipe[0]);
    int left_ws; waitpid(left_pid, &left_ws, 0);

    int right_pipe[2];
    if (pipe(right_pipe) == -1) { perror("pipe right"); free(captured); return 1; }
    pid_t right_pid = fork();
    if (right_pid == 0) {
        close(right_pipe[1]);
        dup2(right_pipe[0], STDIN_FILENO);
        close(right_pipe[0]);
        char right_cmd[4096] = {0};
        for (int i = 0; right_args[i]; i++) {
            if (i > 0) my_strncat(right_cmd, " ", sizeof(right_cmd) - my_strlen(right_cmd) - 1);
            my_strncat(right_cmd, right_args[i], sizeof(right_cmd) - my_strlen(right_cmd) - 1);
        }
        char* ra[4] = { (char*)(self_exe_path() ? self_exe_path() : "./las_shell"), "-c", right_cmd, NULL };
        execvp(ra[0], ra);
        char* rb[4] = { "/bin/sh", "-c", right_cmd, NULL };
        execvp("/bin/sh", rb);
        exit(127);
    }
    if (right_pid < 0) { perror("fork right"); close(right_pipe[0]); close(right_pipe[1]); free(captured); return 1; }
    close(right_pipe[0]);
    size_t off = 0, total = strlen(captured);
    while (off < total) {
        ssize_t w = write(right_pipe[1], captured + off, total - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
    close(right_pipe[1]);
    int right_ws; waitpid(right_pid, &right_ws, 0);
    int right_exit = WIFEXITED(right_ws) ? WEXITSTATUS(right_ws) : 1;

    if (right_exit != 0) {
        const char* reject_reason = (right_exit == 127) ? "CHECKER_NOT_FOUND" : "REJECTED";
        char rpath[512]; snprintf(rpath, sizeof(rpath), "%s/.las_shell_risk_rejections", getenv("HOME") ? getenv("HOME") : "/tmp");
        FILE* rf = fopen(rpath, "a");
        if (rf) { fprintf(rf, "%s | %s\n", reject_reason, captured); fclose(rf); }
        fprintf(stderr, "?> risk gate: %s -- logged to %s\n", reject_reason, rpath);
        free(captured);
        return 1;
    }

    /* PASS -- run `next` exactly once, stdin from captured data, stdout
     * duplicated to both the terminal and the CSV log (single execution,
     * matching execute_with_csv_log's own output format). */
    int stdin_pipe[2], out_pipe[2];
    if (pipe(stdin_pipe) == -1 || pipe(out_pipe) == -1) { perror("pipe next"); free(captured); return 1; }
    pid_t next_pid = fork();
    if (next_pid == 0) {
        close(stdin_pipe[1]); close(out_pipe[0]);
        dup2(stdin_pipe[0], STDIN_FILENO);  close(stdin_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);   close(out_pipe[1]);
        char next_cmd[4096] = {0};
        for (int i = 0; next_args[i]; i++) {
            if (i > 0) my_strncat(next_cmd, " ", sizeof(next_cmd) - my_strlen(next_cmd) - 1);
            my_strncat(next_cmd, next_args[i], sizeof(next_cmd) - my_strlen(next_cmd) - 1);
        }
        char* na[4] = { (char*)(self_exe_path() ? self_exe_path() : "./las_shell"), "-c", next_cmd, NULL };
        execvp(na[0], na);
        char* nb[4] = { "/bin/sh", "-c", next_cmd, NULL };
        execvp("/bin/sh", nb);
        exit(127);
    }
    if (next_pid < 0) { perror("fork next"); free(captured); return 1; }
    close(stdin_pipe[0]); close(out_pipe[1]);
    off = 0;
    while (off < cap_len) {
        ssize_t w = write(stdin_pipe[1], captured + off, cap_len - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
    close(stdin_pipe[1]);
    free(captured);

    FILE* log = fopen(csv_file, "a");
    FILE* pipe_read = fdopen(out_pipe[0], "r");
    char line[4096];
    while (pipe_read && fgets(line, sizeof(line), pipe_read)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        printf("%s\n", line); /* still visible on the terminal, matching prior behavior */
        if (log) {
            time_t now = time(NULL);
            struct tm* lt = localtime(&now);
            char ts[32];
            my_strftime(ts, sizeof(ts), "%Y-%m-%dT%T", lt);
            fprintf(log, "%s,%s\n", ts, line);
        }
    }
    if (pipe_read) fclose(pipe_read); else close(out_pipe[0]);
    if (log) fclose(log);
    int next_ws; waitpid(next_pid, &next_ws, 0);
    return WIFEXITED(next_ws) ? WEXITSTATUS(next_ws) : 1;
}

/* execute_risk_gate_chained() — same as execute_risk_gate(), but if
 * next_args is non-NULL (i.e. 'left ?> checker | next' was written),
 * the captured left-side data is fed into next_args's stdin on PASS
 * instead of being written to the terminal. On REJECT, next_args is
 * never run — rejected orders must not reach execution. */
int execute_risk_gate_chained(char** left_args, char** right_args, char** next_args, char** env) {
    (void)env;
    if (!left_args || !left_args[0] || !right_args || !right_args[0]) return 1;

    /* Reconstruct left command string for las_shell -c
     * Quote each arg that contains spaces, backslashes, or quotes so that
     * /bin/sh -c re-tokenizes it correctly (e.g. printf "SPY\nGME\n"). */
    char left_cmd[4096] = {0};
    for (int i = 0; left_args[i]; i++) {
        if (i > 0) {
            size_t c = strlen(left_cmd);
            if (c + 1 < sizeof(left_cmd) - 1) left_cmd[c] = ' ';
        }
        const char *arg = left_args[i];
        int needs_quote = (strchr(arg, ' ') || strchr(arg, '\t') ||
                           strchr(arg, '\\') || strchr(arg, '"') ||
                           strchr(arg, '\'') || strchr(arg, '\n'));
        size_t c = strlen(left_cmd);
        size_t rem = sizeof(left_cmd) - c - 1;
        if (needs_quote && rem > 2) {
            /* Wrap in single-quotes, escaping any embedded single-quote as '\'' */
            left_cmd[c++] = '\'';
            for (const char *p = arg; *p && c < sizeof(left_cmd) - 2; p++) {
                if (*p == '\'') {
                    /* End quote, escape, reopen: '\'' */
                    if (c + 4 < sizeof(left_cmd) - 1) {
                        left_cmd[c++] = '\'';
                        left_cmd[c++] = '\\';
                        left_cmd[c++] = '\'';
                        left_cmd[c++] = '\'';
                    }
                } else {
                    left_cmd[c++] = *p;
                }
            }
            left_cmd[c++] = '\'';
            left_cmd[c] = '\0';
        } else {
            strncat(left_cmd, arg, rem);
        }
    }

    /* Step 1: run left, capture output */
    int left_pipe[2];
    if (pipe(left_pipe) == -1) { perror("pipe left"); return 1; }
    pid_t left_pid = fork();
    if (left_pid == 0) {
        close(left_pipe[0]);
        dup2(left_pipe[1], STDOUT_FILENO);
        close(left_pipe[1]);
        char* la[4] = {(char*)(self_exe_path() ? self_exe_path() : "./las_shell"), "-c", left_cmd, NULL};
        execvp(la[0], la);
        char* lb[4] = {"/bin/sh",    "-c", left_cmd, NULL};
        execvp("/bin/sh", lb);
        fprintf(stderr, "risk gate: left exec failed: %s\n", left_cmd);
        exit(127);
    }
    if (left_pid < 0) { perror("fork left"); close(left_pipe[0]); close(left_pipe[1]); return 1; }
    close(left_pipe[1]);

    char captured[65536] = "";
    size_t cap_len = 0;
    int truncated = 0;
    char buf[4096];
    ssize_t nr;
    while ((nr = read(left_pipe[0], buf, sizeof(buf)-1)) > 0) {
        buf[nr] = '\0';
        if (cap_len + (size_t)nr < sizeof(captured) - 1) {
            my_strncat(captured, buf, sizeof(captured) - cap_len - 1);
            cap_len += (size_t)nr;
        } else {
            truncated = 1;
        }
    }

    if (truncated) {
        fprintf(stderr, "?> risk gate: WARNING — output truncated at %zu bytes\n",
                sizeof(captured) - 1);
    }
    close(left_pipe[0]);
    int left_ws; waitpid(left_pid, &left_ws, 0);

    /* -- Phase 4.3: validate every captured order line against ~/.las_shell_risk
     * Runs BEFORE the user-supplied right-side checker so shell config limits
     * fire even without an external risk_check script.  Defence-in-depth:    */
    {
        char validate_buf[65536];
        strncpy(validate_buf, captured, sizeof(validate_buf) - 1);
        validate_buf[sizeof(validate_buf) - 1] = '\0';

        char* vline = validate_buf;
        char* vnl;
        int config_rejected = 0;
        while (1) {
            vnl = strchr(vline, '\n');
            if (vnl) *vnl = '\0';
            if (*vline) {
                RiskResult rr;
                if (validate_order_against_risk(vline, &rr) != RISK_PASS) {
                    risk_result_print(&rr);
                    char* home_rej = getenv("HOME");
                    char rej_path[512];
                    if (home_rej)
                        snprintf(rej_path, sizeof(rej_path),
                                 "%s/.las_shell_risk_rejections", home_rej);
                    else
                        strncpy(rej_path, ".las_shell_risk_rejections",
                                sizeof(rej_path) - 1);
                    FILE* rl = fopen(rej_path, "a");
                    if (rl) {
                        time_t now_r = time(NULL);
                        struct tm* lt_r = localtime(&now_r);
                        char ts_r[32];
                        strftime(ts_r, sizeof(ts_r), "%Y-%m-%dT%T", lt_r);
                        fprintf(rl, "%s,CONFIG_LIMIT,%s,%s\n",
                                ts_r, rr.field, vline);
                        fclose(rl);
                    }
                    config_rejected = 1;
                }
            }
            if (!vnl) break;
            vline = vnl + 1;
        }
        if (config_rejected) return 1;
    }

    /* Step 2: feed captured -> right's stdin */
    int right_pipe[2];
    if (pipe(right_pipe) == -1) { perror("pipe right"); return 1; }
    pid_t right_pid = fork();
    if (right_pid == 0) {
        close(right_pipe[1]);
        dup2(right_pipe[0], STDIN_FILENO);
        close(right_pipe[0]);
        execvp(right_args[0], right_args);
        fprintf(stderr, "risk gate: right not found: %s\n", right_args[0]);
        exit(127);
    }
    if (right_pid < 0) { perror("fork right"); close(right_pipe[0]); close(right_pipe[1]); return 1; }
    close(right_pipe[0]);

    size_t written = 0, total = strlen(captured);
    while (written < total) {
        ssize_t w = write(right_pipe[1], captured + written, total - written);
        if (w <= 0) break;
        written += (size_t)w;
    }
    close(right_pipe[1]);

    int right_ws; waitpid(right_pid, &right_ws, 0);
    int right_exit = WIFEXITED(right_ws) ? WEXITSTATUS(right_ws) : 1;

    if (right_exit == 0) {
        /* FIX MS2: ?> is a gate, not a sink. On PASS, the captured left-side
         * output must reach whatever comes next in the chain, exactly like
         * any other Unix filter. Previously this data was only fed to the
         * checker's stdin and then discarded, so 'a ?> check | b' silently
         * gave b an empty input (and 'a ?> check' alone printed nothing). */
        if (next_args && next_args[0]) {
            int next_pipe[2];
            if (pipe(next_pipe) == -1) { perror("pipe next"); return 1; }
            pid_t next_pid = fork();
            if (next_pid == 0) {
                close(next_pipe[1]);
                dup2(next_pipe[0], STDIN_FILENO);
                close(next_pipe[0]);
                char next_cmd[4096] = {0};
                for (int i = 0; next_args[i]; i++) {
                    if (i > 0) my_strncat(next_cmd, " ", sizeof(next_cmd) - my_strlen(next_cmd) - 1);
                    my_strncat(next_cmd, next_args[i], sizeof(next_cmd) - my_strlen(next_cmd) - 1);
                }
                char* na[4] = { (char*)(self_exe_path() ? self_exe_path() : "./las_shell"), "-c", next_cmd, NULL };
                execvp(na[0], na);
                char* nb[4] = { "/bin/sh", "-c", next_cmd, NULL };
                execvp("/bin/sh", nb);
                fprintf(stderr, "?> risk gate: next exec failed: %s\n", next_cmd);
                exit(127);
            }
            if (next_pid < 0) { perror("fork next"); close(next_pipe[0]); close(next_pipe[1]); return 1; }
            close(next_pipe[0]);
            size_t nw = 0, ntotal = strlen(captured);
            while (nw < ntotal) {
                ssize_t w = write(next_pipe[1], captured + nw, ntotal - nw);
                if (w <= 0) break;
                nw += (size_t)w;
            }
            close(next_pipe[1]);
            int next_ws; waitpid(next_pid, &next_ws, 0);
            return WIFEXITED(next_ws) ? WEXITSTATUS(next_ws) : 1;
        }
        size_t to_write = strlen(captured);
        size_t w_off = 0;
        while (w_off < to_write) {
            ssize_t w = write(STDOUT_FILENO, captured + w_off, to_write - w_off);
            if (w <= 0) break;
            w_off += (size_t)w;
        }
        return 0;
    }

    const char* reject_reason = (right_exit == 127) ? "CHECKER_NOT_FOUND" : "REJECTED";

    /* Step 3: rejected — log to ~/.las_shell_risk_rejections */
    char* home = getenv("HOME");
    char log_path[512] = "";
    if (home) {
        my_strncat(log_path, home, sizeof(log_path) - my_strlen(log_path) - 1);
        my_strncat(log_path, "/.las_shell_risk_rejections", sizeof(log_path) - my_strlen(log_path) - 1);
    } else {
        my_strncat(log_path, ".las_shell_risk_rejections", sizeof(log_path) - 1);
    }

    FILE* log = fopen(log_path, "a");
    if (log) {
        time_t now = time(NULL);
        struct tm* lt = localtime(&now);
        char ts[32];
        my_strftime(ts, sizeof(ts), "%Y-%m-%dT%T", lt);
        char tmp[65536];
        my_strcpy(tmp, captured);
        char* line = tmp;
        char* nl;
        while ((nl = strchr(line, '\n')) != NULL) {
            *nl = '\0';
            if (*line) fprintf(log, "%s,%s,%s\n", ts, reject_reason, line);
            line = nl + 1;
        }
        if (*line) fprintf(log, "%s,%s,%s\n", ts, reject_reason, line);
        fclose(log);
        fprintf(stderr, "?> risk gate: %s -- logged to %s\n", reject_reason, log_path);    }
    return 1;
}