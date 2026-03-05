#include "my_own_shell.h"

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
char** split_on_operators(char* input, int* count) {
    char** result = malloc(MAX_INPUT_SIZE * sizeof(char*));
    *count = 0;
    
    char* input_copy = my_strdup(input);
    char* current = input_copy;
    char* start = current;
    int in_operator = 0;
    
    while (*current) {
        if (*current == '&' || *current == '|' || *current == ';') {
            // Sauvegarder la commande précédente
            if (current > start) {
                char* cmd = malloc(current - start + 1);
                strncpy(cmd, start, current - start);
                cmd[current - start] = '\0';
                
                // Enlever les espaces
                char* end = cmd + strlen(cmd) - 1;
                while (end > cmd && (*end == ' ' || *end == '\t')) {
                    *end = '\0';
                    end--;
                }
                
                if (strlen(cmd) > 0) {
                    result[(*count)++] = cmd;
                } else {
                    free(cmd);
                }
            }
            
            // Sauvegarder l'opérateur
            if (*current == '&' && *(current+1) == '&') {
                result[(*count)++] = my_strdup("&&");
                current++;
            } else if (*current == '|' && *(current+1) == '|') {
                result[(*count)++] = my_strdup("||");
                current++;
            } else {
                char op[2] = {*current, '\0'};
                result[(*count)++] = my_strdup(op);
            }
            
            start = current + 1;
        }
        current++;
    }
    
    // Dernière commande
    if (current > start) {
        char* cmd = malloc(current - start + 1);
        strncpy(cmd, start, current - start);
        cmd[current - start] = '\0';
        
        // Enlever les espaces
        char* end = cmd + strlen(cmd) - 1;
        while (end > cmd && (*end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }
        
        if (strlen(cmd) > 0) {
            result[(*count)++] = cmd;
        } else {
            free(cmd);
        }
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
            strcmp(parts[i], "&") == 0 || strcmp(parts[i], ";") == 0) {
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
    CommandNode* current = head;
    int last_status = 0;
    
    while (current != NULL) {
        if (current->args == NULL || current->args[0] == NULL) {
            current = current->next;
            continue;
        }
        
        // Vérifier si on doit exécuter selon l'opérateur précédent
        if (current != head && current->operator) {
            if (strcmp(current->operator, "&&") == 0 && last_status != 0) {
                current = current->next;
                continue;
            }
            if (strcmp(current->operator, "||") == 0 && last_status == 0) {
                current = current->next;
                continue;
            }
        }
        
        clean_jobs();
        
        if (current->background) {
            pid_t pid = fork();
            if (pid == 0) {
                execvp(current->args[0], current->args);
                fprintf(stderr, "Command not found: %s\n", current->args[0]);
                exit(1);
            } else if (pid > 0) {
                char cmd_line[1024] = "";
                for (int i = 0; current->args[i] != NULL; i++) {
                    if (i > 0) strcat(cmd_line, " ");
                    strcat(cmd_line, current->args[i]);
                }
                add_job(pid, cmd_line);
                last_status = 0;
            } else {
                perror("fork");
                last_status = -1;
            }
        } else {
            pid_t pid = fork();
            if (pid == 0) {
                execvp(current->args[0], current->args);
                fprintf(stderr, "Command not found: %s\n", current->args[0]);
                exit(1);
            } else if (pid > 0) {
                int status;
                waitpid(pid, &status, 0);
                last_status = WEXITSTATUS(status);
            } else {
                perror("fork");
                last_status = -1;
            }
        }
        
        current = current->next;
    }
    
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