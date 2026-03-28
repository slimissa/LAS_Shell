#include "my_own_shell.h"

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
        if (c == '|' && c1 == '>') { has_operator = 1; break; }
    }
    if (has_operator) {
        CommandNode* seq = parse_operators(input);
        if (seq) {
            int s = execute_sequence(seq, env);
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

    /* ── QShell trading built-ins ── */
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
        fclose(f);
        return 1;
    }
    if (!(st.st_mode & S_IRUSR)) {
        fprintf(stderr, "las-shell: %s: Permission denied\n", filename);
        fclose(f);
        return 1;
    }

    char line[1024];
    int  exit_status = 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        size_t len = my_strlen(line);
        if (len > 0 && line[len - 1] == '\n') { line[len - 1] = '\0'; len--; }
        if (len == 0 || line[0] == '#') continue;

        char* expanded = expand_aliases(line);
        if (!expanded || expanded[0] == '\0') { free(expanded); continue; }

        /* FIX: expand $() substitutions — without this, 'setenv D $(pwd)'
         * stores the literal string '$(pwd)' instead of the actual path */
        char* with_subs = process_line_with_substitutions(expanded);
        free(expanded);
        if (!with_subs || with_subs[0] == '\0') { free(with_subs); continue; }

        add_to_history(line);

        /* FIX: pass &env so setenv/unsetenv results persist to next line */
        exit_status = execute_command_line_env(with_subs, &env);
        update_exit_status(exit_status);
        free(with_subs);
    }

    fclose(f);
    return exit_status;
}