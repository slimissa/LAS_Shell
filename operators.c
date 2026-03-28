#include "my_own_shell.h"

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

    char* input_copy = my_strdup(input);
    char* current = input_copy;
    char* start = current;

    /* track quote state so operators inside quotes are ignored */
    int  in_quotes  = 0;
    char quote_char = 0;

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

        /* operator detection — single | is NOT an operator here */
        int   op_len = 0;
        char  op_str[3] = {0};

        if (*current == '?' && *(current+1) == '>') {
            op_len = 2; op_str[0] = '?'; op_str[1] = '>';
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
        }
        /* single | is deliberately skipped — it belongs to parse_pipeline */

        if (op_len > 0) {
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
    
    for (int i = 0; i < part_count; i++) {
        if (strcmp(parts[i], "&&") == 0 || strcmp(parts[i], "||") == 0 || 
            strcmp(parts[i], "&") == 0  || strcmp(parts[i], ";") == 0  ||
            strcmp(parts[i], "|>") == 0 || strcmp(parts[i], "?>") == 0) {
            // C'est un opérateur, on l'ajoute au nœud précédent
            if (current) {
                current->operator = my_strdup(parts[i]);
            }
        } else {
            // C'est une commande
            CommandNode* node = malloc(sizeof(CommandNode));
            memset(node, 0, sizeof(CommandNode));
            
            // Parser la commande
            node->args = parse_input(parts[i]);
            
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

int execute_sequence(CommandNode* head, char** env) {
    CommandNode* node = head;
    CommandNode* prev = NULL;   /* FIX: track previous node to read its operator */
    int last_status = 0;

    char* initial_dir = getcwd(NULL, 0);

    while (node != NULL) {
        if (node->args == NULL || node->args[0] == NULL) {
            prev = node;
            node = node->next;
            continue;
        }

        /* FIX BUG: operator is stored on PREV node, not current node */
        if (prev != NULL && prev->operator != NULL) {
            if (strcmp(prev->operator, "&&") == 0 && last_status != 0) {
                prev = node; node = node->next; continue;
            }
            if (strcmp(prev->operator, "||") == 0 && last_status == 0) {
                prev = node; node = node->next; continue;
            }
            if (strcmp(prev->operator, "|>") == 0) {
                if (node->args && node->args[0])
                    last_status = execute_with_csv_log(prev->args, env, node->args[0]);
                prev = node; node = node->next; continue;
            }
            if (strcmp(prev->operator, "?>") == 0) {
                if (node->args && node->args[0])
                    last_status = execute_risk_gate(prev->args, node->args, env);
                else last_status = 1;
                prev = node; node = node->next; continue;
            }
        }

        clean_jobs();

        if (node->background) {
            pid_t pid = fork();
            if (pid == 0) {
                execvp(node->args[0], node->args);
                fprintf(stderr, "Command not found: %s\n", node->args[0]);
                exit(127);
            } else if (pid > 0) {
                char cmd_line[1024] = "";
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
                char full_cmd[1024] = "";
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
            int builtin_ret = shell_builtins(exec_args, env, initial_dir);
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
        free(current);
        current = next;
    }
}