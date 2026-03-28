#include "my_own_shell.h"

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
    int pipefd[2];
    if (pipe(pipefd) == -1) { perror("pipe"); return 1; }
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execvp(args[0], args);
        fprintf(stderr, "Command not found: %s\n", args[0]);
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
 *    right exit !=0 → log to ~/.qshell_risk_rejections, return 1.
 * ─────────────────────────────────────────────────────────────── */
int execute_risk_gate(char** left_args, char** right_args, char** env) {
    (void)env;
    if (!left_args || !left_args[0] || !right_args || !right_args[0]) return 1;

    /* Step 1: run left, capture output */
    int left_pipe[2];
    if (pipe(left_pipe) == -1) { perror("pipe left"); return 1; }
    pid_t left_pid = fork();
    if (left_pid == 0) {
        close(left_pipe[0]);
        dup2(left_pipe[1], STDOUT_FILENO);
        close(left_pipe[1]);
        execvp(left_args[0], left_args);
        fprintf(stderr, "risk gate: left not found: %s\n", left_args[0]);
        exit(127);
    }
    if (left_pid < 0) { perror("fork left"); close(left_pipe[0]); close(left_pipe[1]); return 1; }
    close(left_pipe[1]);

    char captured[65536] = "";
    size_t cap_len = 0;
    char buf[4096];
    ssize_t nr;
    while ((nr = read(left_pipe[0], buf, sizeof(buf)-1)) > 0) {
        buf[nr] = '\0';
        if (cap_len + (size_t)nr < sizeof(captured) - 1) {
            my_strncat(captured, buf, sizeof(captured) - cap_len - 1);
            cap_len += (size_t)nr;
        }
    }
    close(left_pipe[0]);
    int left_ws; waitpid(left_pid, &left_ws, 0);

    /* Step 2: feed captured → right's stdin */
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

    /* Step 3: rejected — log to ~/.qshell_risk_rejections */
    char* home = getenv("HOME");
    char log_path[512] = "";
    if (home) {
        my_strncat(log_path, home, sizeof(log_path) - my_strlen(log_path) - 1);
        my_strncat(log_path, "/.qshell_risk_rejections", sizeof(log_path) - my_strlen(log_path) - 1);
    } else {
        my_strncat(log_path, ".qshell_risk_rejections", sizeof(log_path) - 1);
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
            if (*line) fprintf(log, "%s,REJECTED,%s\n", ts, line);
            line = nl + 1;
        }
        if (*line) fprintf(log, "%s,REJECTED,%s\n", ts, line);
        fclose(log);
        fprintf(stderr, "?> risk gate: REJECTED -- logged to %s\n", log_path);
    }
    return 1;
}