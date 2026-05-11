#include "../include/my_own_shell.h"

// Fonction pour exécuter un pipeline
int execute_pipeline(char*** commands, int cmd_count, char** env) {
    (void)env;
    int i;
    int prev_pipe[2] = {-1, -1};  // Pipe précédent (lecture)
    pid_t pids[cmd_count];
    
    for (i = 0; i < cmd_count; i++) {
        int current_pipe[2] = {-1, -1};
        
        // Créer un pipe pour toutes les commandes sauf la dernière
        if (i < cmd_count - 1) {
            if (pipe(current_pipe) == -1) {
                perror("pipe");
                return -1;
            }
        }
        
        pids[i] = fork();
        
        if (pids[i] == 0) {
            // PROCESSUS ENFANT
            
            // Rediriger l'entrée depuis le pipe précédent (sauf première commande)
            if (i > 0) {
                dup2(prev_pipe[0], STDIN_FILENO);
                close(prev_pipe[0]);
                close(prev_pipe[1]);
            }
            
            // Rediriger la sortie vers le pipe courant (sauf dernière commande)
            if (i < cmd_count - 1) {
                dup2(current_pipe[1], STDOUT_FILENO);
                close(current_pipe[0]);
                close(current_pipe[1]);
            }
            
            // Exécuter la commande
            execvp(commands[i][0], commands[i]);
            fprintf(stderr, "Command not found: %s\n", commands[i][0]);
            exit(1);
        }
        else if (pids[i] > 0) {
            // PROCESSUS PARENT
            
            // Fermer le pipe précédent (plus besoin)
            if (i > 0) {
                close(prev_pipe[0]);
                close(prev_pipe[1]);
            }
            
            // Sauvegarder le pipe courant pour la prochaine itération
            if (i < cmd_count - 1) {
                prev_pipe[0] = current_pipe[0];
                prev_pipe[1] = current_pipe[1];
            }
        }
        else {
            perror("fork");
            return -1;
        }
    }
    
    // Le parent attend que tous les enfants finissent
    int last_status = 0;
    for (i = 0; i < cmd_count; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == cmd_count - 1) {
            last_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
    }
    
    return last_status;
}

// Fonction pour parser une ligne avec des pipes
Pipeline* parse_pipeline(char* input) {
    Pipeline* pipeline = malloc(sizeof(Pipeline));
    if (!pipeline) return NULL;
    
    /* ── FIX: track quote state when counting pipes ── */
    int pipe_count = 0;
    int in_sq = 0, in_dq = 0;
    for (int i = 0; input[i]; i++) {
        if (input[i] == '\'' && !in_dq) { in_sq = !in_sq; continue; }
        if (input[i] == '"'  && !in_sq) { in_dq = !in_dq; continue; }
        if (!in_sq && !in_dq && input[i] == '|') pipe_count++;
    }
    
    pipeline->cmd_count = pipe_count + 1;
    pipeline->commands = malloc(pipeline->cmd_count * sizeof(char**));
    
    if (!pipeline->commands) {
        free(pipeline);
        return NULL;
    }
    
    char* input_copy = my_strdup(input);
    char* current = input_copy;
    char* start = current;
    int cmd_index = 0;
    
    in_sq = 0; in_dq = 0;
    while (*current && cmd_index < pipeline->cmd_count) {
        if (*current == '\'' && !in_dq) { in_sq = !in_sq; current++; continue; }
        if (*current == '"'  && !in_sq) { in_dq = !in_dq; current++; continue; }
        
        if (!in_sq && !in_dq && *current == '|') {
            /* end of command segment — extract it */
            *current = '\0';
            
            /* trim leading spaces */
            while (*start == ' ') start++;
            /* trim trailing spaces */
            char* end = start + strlen(start) - 1;
            while (end > start && *end == ' ') { *end = '\0'; end--; }
            
            if (*start) {
                pipeline->commands[cmd_index] = parse_input(start);
                cmd_index++;
            }
            start = current + 1;
        }
        current++;
    }
    
    /* last segment */
    if (*start && cmd_index < pipeline->cmd_count) {
        while (*start == ' ') start++;
        char* end = start + strlen(start) - 1;
        while (end > start && *end == ' ') { *end = '\0'; end--; }
        if (*start)
            pipeline->commands[cmd_index++] = parse_input(start);
    }
    pipeline->cmd_count = cmd_index;
    
    free(input_copy);
    return pipeline;
}

// Libérer la mémoire d'un pipeline
void free_pipeline(Pipeline* pipeline) {
    if (!pipeline) return;
    
    for (int i = 0; i < pipeline->cmd_count; i++) {
        free_tokens(pipeline->commands[i]);
    }
    free(pipeline->commands);
    free(pipeline);
}