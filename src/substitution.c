#include "../include/my_own_shell.h"

// Fonction pour exécuter une commande et capturer sa sortie
char* execute_and_capture(const char* cmd) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return NULL;
    }
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Processus enfant
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        
        // Pour les substitutions imbriquées, on utilise notre propre shell
        char* args[4] = {"./las_shell", "-c", (char*)cmd, NULL};
        execvp(args[0], args);
        
        // Fallback sur sh
        char* fallback[4] = {"/bin/sh", "-c", (char*)cmd, NULL};
        execvp(fallback[0], fallback);
        
        fprintf(stderr, "Command failed: %s\n", cmd);
        exit(1);
    }
    else if (pid > 0) {
        close(pipefd[1]);
        
        char buffer[4096];
        char* result = malloc(1);
        if (!result) return NULL;
        result[0] = '\0';
        size_t total_len = 0;
        
        ssize_t count;
        while ((count = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[count] = '\0';
            total_len += count;
            char* new_result = realloc(result, total_len + 1);
            if (!new_result) {
                free(result);
                close(pipefd[0]);
                return NULL;
            }
            result = new_result;
            strcat(result, buffer);
        }
        
        // Enlever le newline final
        size_t len = strlen(result);
        while (len > 0 && result[len-1] == '\n') {
            result[--len] = '\0';
        }
        
        close(pipefd[0]);
        
        int status;
        waitpid(pid, &status, 0);
        
        return result;
    }
    else {
        perror("fork");
        return NULL;
    }
}

// Fonction pour trouver la prochaine substitution $(...)
char* find_next_substitution(const char* input, int* start_pos, int* end_pos) {
    *start_pos = -1;
    *end_pos = -1;
    
    if (!input) return NULL;
    
    int len = strlen(input);
    int in_single_quotes = 0;
    int in_double_quotes = 0;
    
    for (int i = 0; i < len; i++) {
        if (input[i] == '\'') {
            in_single_quotes = !in_single_quotes;
            continue;
        }
        if (input[i] == '"') {
            in_double_quotes = !in_double_quotes;
            continue;
        }
        
        if (!in_single_quotes && input[i] == '$' && i + 1 < len && input[i+1] == '(') {
            *start_pos = i;
            int depth = 1;
            int j = i + 2;
            
            while (j <= len && depth > 0) {
                if (j < len && input[j] == '(') depth++;
                else if (j < len && input[j] == ')') depth--;
                else if (j == len) break;  /* reached end without closing */
                j++;
            }
            
            if (depth == 0) {
                *end_pos = j - 1;
                char* cmd = malloc(j - i - 1);
                if (!cmd) return NULL;
                strncpy(cmd, input + i + 2, j - i - 3);
                cmd[j - i - 3] = '\0';
                return cmd;
            }
        }
    }
    
    return NULL;
}

// Fonction pour expandre les substitutions de l'intérieur vers l'extérieur
char* expand_nested_substitutions(const char* input) {
    if (!input) return my_strdup("");
    
    char* result = my_strdup(input);
    if (!result) return NULL;
    
    int changed;
    int max_iter = 10;
    
    do {
        changed = 0;
        char* new_result = malloc(1);
        if (!new_result) {
            free(result);
            return NULL;
        }
        new_result[0] = '\0';
        
        const char* current = result;
        
        while (*current) {
            if (*current == '$' && *(current+1) == '(') {
                // Trouver la substitution complète
                const char* start = current;
                current += 2;
                int depth = 1;
                
                while (*current && depth > 0) {
                    if (*current == '(') depth++;
                    else if (*current == ')') depth--;
                    current++;
                }
                
                // Extraire la commande interne
                int cmd_len = current - start - 3;
                char* cmd = malloc(cmd_len + 1);
                if (!cmd) {
                    free(new_result);
                    free(result);
                    return NULL;
                }
                strncpy(cmd, start + 2, cmd_len);
                cmd[cmd_len] = '\0';
                
                // Vérifier si la commande contient elle-même des substitutions
                char* expanded_cmd;
                if (strstr(cmd, "$(") != NULL) {
                    // Récursion : d'abord expandre les substitutions internes
                    expanded_cmd = expand_nested_substitutions(cmd);
                } else {
                    expanded_cmd = my_strdup(cmd);
                }
                
                // Exécuter la commande expandée
                char* output = execute_and_capture(expanded_cmd);
                
                if (output) {
                    // Concaténer le résultat
                    size_t new_len = strlen(new_result) + strlen(output);
                    char* temp = realloc(new_result, new_len + 1);
                    if (!temp) {
                        free(new_result);
                        free(expanded_cmd);
                        free(cmd);
                        free(output);
                        free(result);
                        return NULL;
                    }
                    new_result = temp;
                    strcat(new_result, output);
                    free(output);
                    changed = 1;
                }
                
                free(expanded_cmd);
                free(cmd);
            } else {
                // Copier le caractère normal
                size_t len = strlen(new_result);
                char* temp = realloc(new_result, len + 2);
                if (!temp) {
                    free(new_result);
                    free(result);
                    return NULL;
                }
                new_result = temp;
                new_result[len] = *current;
                new_result[len + 1] = '\0';
                current++;
            }
        }
        
        free(result);
        result = new_result;
        max_iter--;
        
    } while (changed && strstr(result, "$(") != NULL && max_iter > 0);
    
    return result;
}

// Fonction principale pour traiter une ligne avec substitutions
// Handles both regular $() and streaming $<() substitutions.
// NOTE: For $<() inside while loops, the script interpreter manages
//       StreamSub state directly — this path covers the one-shot case.
char* process_line_with_substitutions(char* input) {
    if (!input || input[0] == '\0') return my_strdup(input);

    /* ── Fast-path: nothing to expand ── */
    int has_regular  = (strstr(input, "$(")  != NULL);
    int has_streaming = (strstr(input, "$<(") != NULL);

    if (!has_regular && !has_streaming)
        return my_strdup(input);

    char* result = my_strdup(input);
    if (!result) return NULL;

    /* ── Step 1: expand $<() one-shot substitutions first ──
     *   We do these BEFORE regular $() so that a line like
     *       x=$(echo $<(seq 1 3))
     *   has the $<() resolved first (result: "1"), then the $() picks up.
     */
    if (has_streaming) {
        char* after_streaming = process_line_with_streaming_subs(result, NULL);
        free(result);
        result = after_streaming ? after_streaming : my_strdup("");
    }

    /* ── Step 2: expand regular $() substitutions ── */
    if (strstr(result, "$(") != NULL) {
        char* after_regular = expand_nested_substitutions(result);
        free(result);
        result = after_regular ? after_regular : my_strdup("");
    }

    return result;
}