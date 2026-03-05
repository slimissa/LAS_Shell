#include "my_own_shell.h"

// Fonction pour exécuter un pipeline
int execute_pipeline(char*** commands, int cmd_count, char** env) {
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
    for (i = 0; i < cmd_count; i++) {
        int status;
        waitpid(pids[i], &status, 0);
    }
    
    return 0;
}

// Fonction pour parser une ligne avec des pipes
Pipeline* parse_pipeline(char* input) {
    Pipeline* pipeline = malloc(sizeof(Pipeline));
    if (!pipeline) return NULL;
    
    // Compter le nombre de pipes
    int pipe_count = 0;
    for (int i = 0; input[i]; i++) {
        if (input[i] == '|') pipe_count++;
    }
    
    pipeline->cmd_count = pipe_count + 1;
    pipeline->commands = malloc(pipeline->cmd_count * sizeof(char**));
    
    if (!pipeline->commands) {
        free(pipeline);
        return NULL;
    }
    
    // Découper la ligne sur les pipes
    char* input_copy = my_strdup(input);
    char* saveptr;
    char* cmd_str = my_strtok(input_copy, "|", &saveptr);
    
    int cmd_index = 0;
    while (cmd_str != NULL && cmd_index < pipeline->cmd_count) {
        // Enlever les espaces au début et à la fin
        while (*cmd_str == ' ') cmd_str++;
        char* end = cmd_str + my_strlen(cmd_str) - 1;
        while (end > cmd_str && *end == ' ') {
            *end = '\0';
            end--;
        }
        
        // Parser chaque commande individuellement
        pipeline->commands[cmd_index] = parse_input(cmd_str);
        cmd_index++;
        
        cmd_str = my_strtok(NULL, "|", &saveptr);
    }
    
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