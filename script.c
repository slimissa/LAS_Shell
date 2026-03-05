#include "my_own_shell.h"

// Exécuter un fichier de script
int execute_script(char* filename, char** env) {
    FILE* script = fopen(filename, "r");
    if (!script) {
        fprintf(stderr, "las-shell: %s: No such file or directory\n", filename);
        return -1;
    }
    
    // Vérifier les permissions (script doit être lisible)
    struct stat st;
    if (stat(filename, &st) == -1) {
        fprintf(stderr, "las-shell: %s: Cannot access file\n", filename);
        fclose(script);
        return -1;
    }
    
    if (!(st.st_mode & S_IRUSR)) {
        fprintf(stderr, "las-shell: %s: Permission denied\n", filename);
        fclose(script);
        return -1;
    }
    
    char line[1024];
    int line_num = 0;
    int exit_status = 0;
    
    printf("=== Exécution du script: %s ===\n", filename);
    
    while (fgets(line, sizeof(line), script) != NULL) {
        line_num++;
        
        // Enlever le newline
        size_t len = my_strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
            len--;
        }
        
        // Ignorer les lignes vides et les commentaires
        if (len == 0 || line[0] == '#') {
            continue;
        }
        
        printf("[ligne %d] %s\n", line_num, line);
        
        // Exécuter la commande
        char* expanded_input = expand_aliases(line);
        if (!expanded_input || expanded_input[0] == '\0') {
            free(expanded_input);
            continue;
        }
        
        // Ajouter à l'historique
        add_to_history(line);
        
        // Parser et exécuter
        exit_status = execute_command_line(expanded_input, env);
        free(expanded_input);
        
        // Si erreur, on peut choisir de continuer ou pas
        // Pour l'instant, on continue
    }
    
    printf("=== Fin du script: %s ===\n", filename);
    fclose(script);
    return exit_status;
}

// Fonction pour exécuter une ligne de commande (réutilisable)
int execute_command_line(char* input, char** env) {
    // ========== DÉTECTION DES OPÉRATEURS ==========
    int has_operator = 0;
    for (int i = 0; input[i]; i++) {
        if (input[i] == '&' || input[i] == '|' || input[i] == ';') {
            if (input[i] == '|' && (i == 0 || input[i-1] != '|')) {
                continue;
            }
            has_operator = 1;
            break;
        }
    }
    
    if (has_operator) {
        CommandNode* sequence = parse_operators(input);
        if (sequence) {
            int status = execute_sequence(sequence, env);
            free_sequence(sequence);
            return status;
        }
        return 0;
    }
    
    // ========== DÉTECTION DES PIPES ==========
    int has_pipe = 0;
    for (int i = 0; input[i]; i++) {
        if (input[i] == '|' && (i == 0 || input[i-1] != '|')) {
            has_pipe = 1;
            break;
        }
    }
    
    if (has_pipe) {
        Pipeline* pipeline = parse_pipeline(input);
        if (pipeline) {
            int status = execute_pipeline(pipeline->commands, pipeline->cmd_count, env);
            free_pipeline(pipeline);
            return status;
        }
        return 0;
    }
    
    // ========== PARSING NORMAL ==========
    char** args = parse_input(input);
    if (!args || !args[0]) {
        if (args) free_tokens(args);
        return 0;
    }
    
    // Détection des redirections
    char* output_file = NULL;
    char* input_file = NULL;
    int append_mode = 0;
    int has_output_redirect = 0;
    int has_input_redirect = 0;
    char** clean_args = NULL;
    
    // Compter les arguments sans les redirections
    int arg_count = 0;
    for (int i = 0; args[i] != NULL; i++) {
        if (my_strcmp(args[i], ">") == 0 || 
            my_strcmp(args[i], ">>") == 0 || 
            my_strcmp(args[i], "<") == 0) {
            i++;
        } else {
            arg_count++;
        }
    }
    
    clean_args = malloc((arg_count + 1) * sizeof(char*));
    if (!clean_args) {
        perror("malloc");
        free_tokens(args);
        return -1;
    }
    
    int j = 0;
    for (int i = 0; args[i] != NULL; i++) {
        if (my_strcmp(args[i], ">") == 0) {
            has_output_redirect = 1;
            append_mode = 0;
            if (args[i+1] != NULL) output_file = my_strdup(args[i+1]);
            i++;
        }
        else if (my_strcmp(args[i], ">>") == 0) {
            has_output_redirect = 1;
            append_mode = 1;
            if (args[i+1] != NULL) output_file = my_strdup(args[i+1]);
            i++;
        }
        else if (my_strcmp(args[i], "<") == 0) {
            has_input_redirect = 1;
            if (args[i+1] != NULL) input_file = my_strdup(args[i+1]);
            i++;
        }
        else {
            clean_args[j++] = my_strdup(args[i]);
        }
    }
    clean_args[j] = NULL;
    
    int status = 0;
    
    // Exécution
    if (has_input_redirect || has_output_redirect) {
        if (has_input_redirect && has_output_redirect) {
            execute_with_both_redirect(clean_args, env, input_file, output_file, append_mode);
        }
        else if (has_input_redirect) {
            execute_with_input_redirect(clean_args, env, input_file);
        }
        else if (has_output_redirect) {
            execute_with_redirect(clean_args, env, output_file, append_mode);
        }
    } else {
        char* initial_directory = getcwd(NULL, 0);
        if (shell_builtins(args, env, initial_directory) == -1) {
            pid_t pid = fork();
            if (pid == 0) {
                execvp(args[0], args);
                fprintf(stderr, "Command not found: %s\n", args[0]);
                exit(1);
            } else if (pid > 0) {
                int wstatus;
                waitpid(pid, &wstatus, 0);
                status = WEXITSTATUS(wstatus);
            } else {
                perror("fork");
                status = -1;
            }
        }
        free(initial_directory);
    }
    
    // Nettoyage
    if (output_file) free(output_file);
    if (input_file) free(input_file);
    if (clean_args) {
        for (int i = 0; clean_args[i] != NULL; i++) free(clean_args[i]);
        free(clean_args);
    }
    free_tokens(args);
    
    return status;
}