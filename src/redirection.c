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
            if (append_mode) {
                fd = open(output_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
            } else {
                fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
                fd_out = open(output_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
            } else {
                fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
/* ── execute_with_csv_log() ────────────────────────────────────────── */
int execute_with_csv_log(char** args, char** env, const char* csv_file) {
    (void)env;
    if (!args || !args[0] || !csv_file) return 1;

    /* Reconstruct the full command string so we can run it via
     * 'las_shell -c <cmd>'.  This is ESSENTIAL for broker builtins
     * (order, positions, balance, etc.) which only exist inside the
     * shell process — bare execvp("order",...) would fail with
     * "Command not found".  By routing through las_shell -c we let
     * the full builtin dispatch table handle the command.            */
    char cmd_line[4096] = {0};
    for (int i = 0; args[i]; i++) {
        if (i > 0) {
            size_t cur = strlen(cmd_line);
            if (cur + 1 < sizeof(cmd_line) - 1)
                cmd_line[cur] = ' ';
        }
        size_t cur = strlen(cmd_line);
        size_t rem = sizeof(cmd_line) - cur - 1;
        strncat(cmd_line, args[i], rem);
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
        char* las_args[4] = { "./las_shell", "-c", cmd_line, NULL };
        execvp("./las_shell", las_args);
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
        char* la[4] = {"./las_shell", "-c", left_cmd, NULL};
        execvp("./las_shell", la);
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

    if (right_exit == 0) return 0;

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