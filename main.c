#include "my_own_shell.h"

static char* expand_tilde_main(const char* path) {
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



//cd pwd echo env setenv unsetenv which exit jobs fg bg

int shell_builtins(char** args, char** env, char* initial_directory) {
    if (my_strcmp(args[0], "cd") == 0) {
        return command_cd(args, initial_directory, env);
    }
    else if (my_strcmp(args[0], "pwd") == 0) {
        return command_pwd();
    }
    else if (my_strcmp(args[0], "echo") == 0) {
        return command_echo(args, env);
    } 
    else if (my_strcmp(args[0], "env") == 0) {
        return command_env(env);
    } 
    else if (my_strcmp(args[0], "which") == 0) {
        return command_which(args, env);
    } 
    else if (my_strcmp(args[0], "alias") == 0) {
        return command_alias(args);
    }
    else if (my_strcmp(args[0], "unalias") == 0) {
        return command_unalias(args);
    }
    else if (my_strcmp(args[0], "source") == 0 || my_strcmp(args[0], ".") == 0) {
        return command_source(args, env);
    }
    else if (my_strcmp(args[0], "exit") == 0) {
        save_history();
        save_aliases();
        return command_exit(args);
    }
    else if (my_strcmp(args[0], "jobs") == 0) {
        return command_jobs(args, env);
    }
    else if (my_strcmp(args[0], "fg") == 0) {
        return command_fg(args, env);
    }
    else if (my_strcmp(args[0], "bg") == 0) {
        return command_bg(args, env);
    }
    else if (my_strcmp(args[0], "assert") == 0) {
        return command_assert(args, env);
    }
    else if (my_strcmp(args[0], "work") == 0) {
        return command_work(args);
    }
    else if (my_strcmp(args[0], "watch") == 0) {
        return command_watch(args, env);
    }
    else {
        return -1;
    }
    return 0;
}

void shell_loop(char** env) {
    char* input = NULL;
    char* initial_directory = getcwd(NULL, 0);
    char* expanded_input = NULL;
    char* with_substitutions = NULL;
    char** args = NULL;
    int last_status = 0;
    
    // Initialiser l'historique, les aliases et les infos du prompt
    init_history();
    init_aliases();
    init_prompt_info();
    
    // Configurer le signal Ctrl+C
    signal(SIGINT, handle_sigint);

    while (1) {
        // Nettoyer les jobs
        clean_jobs();
        
        // Lire l'entrée avec readline (utilise generate_prompt() automatiquement)
        input = read_input();

        // Gérer Ctrl+D (EOF)
        if (input == NULL) {
            printf("\n");
            break;
        }

        // Ignorer les lignes vides
        if (input[0] == '\0') {
            free(input);
            continue;
        }

        // Sauvegarder la commande originale pour l'historique
        char* original_input = my_strdup(input);

        // ========== ÉTAPE 1: EXPANSION DES ALIASES ==========
        expanded_input = expand_aliases(input);
        free(input);

        if (!expanded_input || expanded_input[0] == '\0') {
            free(expanded_input);
            free(original_input);
            continue;
        }

        // ========== ÉTAPE 2: EXPANSION DES SUBSTITUTIONS ==========
        with_substitutions = process_line_with_substitutions(expanded_input);
        free(expanded_input);

        if (!with_substitutions || with_substitutions[0] == '\0') {
            free(with_substitutions);
            free(original_input);
            continue;
        }

        // Ajouter la commande originale à l'historique
        if (original_input) {
            add_to_history(original_input);
            free(original_input);
        }

        // Utiliser with_substitutions pour la suite
        input = with_substitutions;

        // ========== EXPANSION $VAR sur la ligne brute ==========
        // Expand BEFORE operator detection so |> $FILE and ?> work
        {
            char* expanded_line = expand_vars_in_line(input, env);
            if (expanded_line && expanded_line != input) {
                free(input);
                input = expanded_line;
            }
        }

        // ========== DÉTECTION DES OPÉRATEURS (&&, ||, &, ;) ==========
        // FIX: single | is NOT an operator — only ||, &&, ;, & are
        int has_operator = 0;
        for (int i = 0; input[i]; i++) {
            char c  = input[i];
            char c1 = input[i+1];
            if (c == ';') { has_operator = 1; break; }
            if (c == '&') { has_operator = 1; break; }
            if (c == '|' && c1 == '|') { has_operator = 1; break; }
            if (c == '|' && c1 == '>') { has_operator = 1; break; }
            if (c == '?' && c1 == '>') { has_operator = 1; break; }
        }

        if (has_operator) {
            CommandNode* sequence = parse_operators(input);
            if (sequence) {
                last_status = execute_sequence(sequence, env);
                free_sequence(sequence);
            }
            free(input);
            update_exit_status(last_status);
            continue;
        }

        // ========== DÉTECTION DES PIPES (|) ==========
        int has_pipe = 0;
        for (int i = 0; input[i]; i++) {
            if (input[i] == '|' && (i == 0 || input[i-1] != '|') && input[i+1] != '>') {
                has_pipe = 1;
                break;
            }
        }

        if (has_pipe) {
            Pipeline* pipeline = parse_pipeline(input);
            if (pipeline) {
                last_status = execute_pipeline(pipeline->commands, pipeline->cmd_count, env);
                free_pipeline(pipeline);
            }
            free(input);
            update_exit_status(last_status);
            continue;
        }

        // ========== PARSING NORMAL ==========
        args = parse_input(input);
        
        if (args == NULL) {
            free(input);
            continue;
        }
        
        if (args[0] == NULL) {
            free_tokens(args);
            free(input);
            continue;
        }

        // ========== EXPANSION $VAR DANS LES ARGS ==========
        expand_args(args, env);

        // ========== COMMANDES BUILT-IN ==========
        if (my_strcmp(args[0], "history") == 0) {
            HIST_ENTRY **hist_list = history_list();
            if (hist_list) {
                for (int i = 0; hist_list[i] != NULL; i++) {
                    printf("%d  %s\n", i + 1, hist_list[i]->line);
                }
            }
            last_status = 0;
        }
        else if (my_strcmp(args[0], "jobs") == 0) {
            last_status = command_jobs(args, env);
        }
        else if (my_strcmp(args[0], "fg") == 0) {
            last_status = command_fg(args, env);
        }
        else if (my_strcmp(args[0], "bg") == 0) {
            last_status = command_bg(args, env);
        }
        else if (my_strcmp(args[0], "setenv") == 0) {
            env = command_setenv(args, env);
            last_status = 0;
        }
        else if (my_strcmp(args[0], "unsetenv") == 0) {
            env = command_unsetenv(args, env);
            last_status = 0;
        }
        else if (my_strcmp(args[0], "setmarket") == 0) {
            env = command_setmarket(args, env); last_status = 0;
        }
        else if (my_strcmp(args[0], "setbroker") == 0) {
            env = command_setbroker(args, env); last_status = 0;
        }
        else if (my_strcmp(args[0], "setaccount") == 0) {
            env = command_setaccount(args, env); last_status = 0;
        }
        else if (my_strcmp(args[0], "setcapital") == 0) {
            env = command_setcapital(args, env); last_status = 0;
        }
        /* ── assert: must be handled BEFORE redirection detection
         * because '>' and '<' in  "assert 5 > 3"  would be mistaken
         * for output/input redirections otherwise.               ── */
        else if (my_strcmp(args[0], "assert") == 0) {
            last_status = command_assert(args, env);
        }
        else if (args[0][0] == '@') {
            if (wait_until(args[0]) == 0) {
                if (args[1]) {
                    char remaining[1024] = "";
                    for (int i = 1; args[i]; i++) {
                        if (i > 1) my_strncat(remaining, " ", sizeof(remaining)-my_strlen(remaining)-1);
                        my_strncat(remaining, args[i], sizeof(remaining)-my_strlen(remaining)-1);
                    }
                    last_status = execute_command_line(remaining, env);
                }
            } else {
                fprintf(stderr, "@time: invalid format '%s'\n", args[0]);
                last_status = 1;
            }
        }
        else {
            // ========== DETECTION DES REDIRECTIONS ==========
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
                free(input);
                update_exit_status(1);
                continue;
            }

            int j = 0;
            for (int i = 0; args[i] != NULL; i++) {
                if (my_strcmp(args[i], ">") == 0) {
                    has_output_redirect = 1;
                    append_mode = 0;
                    if (args[i+1] != NULL) output_file = expand_tilde_main(args[i+1]);
                    i++;
                }
                else if (my_strcmp(args[i], ">>") == 0) {
                    has_output_redirect = 1;
                    append_mode = 1;
                    if (args[i+1] != NULL) output_file = expand_tilde_main(args[i+1]);
                    i++;
                }
                else if (my_strcmp(args[i], "<") == 0) {
                    has_input_redirect = 1;
                    if (args[i+1] != NULL) input_file = expand_tilde_main(args[i+1]);
                    i++;
                }
                else {
                    clean_args[j++] = my_strdup(args[i]);
                }
            }
            clean_args[j] = NULL;

            // ========== EXÉCUTION AVEC/SANS REDIRECTIONS ==========
            if (has_input_redirect || has_output_redirect) {
                if (has_input_redirect && has_output_redirect) {
                    last_status = execute_with_both_redirect(clean_args, env, input_file, output_file, append_mode);
                }
                else if (has_input_redirect) {
                    last_status = execute_with_input_redirect(clean_args, env, input_file);
                }
                else if (has_output_redirect) {
                    last_status = execute_with_redirect(clean_args, env, output_file, append_mode);
                }
            } else {
                // Pas de redirection, exécution normale
                if (shell_builtins(args, env, initial_directory) == -1) {
                    pid_t pid = fork();
                    if (pid == 0) {
                        execvp(args[0], args);
                        fprintf(stderr, "Command not found: %s\n", args[0]);
                        exit(1);
                    } else if (pid > 0) {
                        int status;
                        waitpid(pid, &status, 0);
                        last_status = WEXITSTATUS(status);
                    } else {
                        perror("fork");
                        last_status = -1;
                    }
                } else {
                    last_status = 0;
                }
            }

            // ========== NETTOYAGE DES REDIRECTIONS ==========
            if (output_file) free(output_file);
            if (input_file) free(input_file);
            if (clean_args) {
                for (int i = 0; clean_args[i] != NULL; i++) free(clean_args[i]);
                free(clean_args);
            }
        }

        // Mettre à jour le code de retour pour le prompt
        update_exit_status(last_status);

        // ========== NETTOYAGE FINAL ==========
        free_tokens(args);
        free(input);
        args = NULL;
    }

    // ========== NETTOYAGE À LA SORTIE ==========
    cleanup_history();
    cleanup_aliases();
    if (initial_directory != NULL) free(initial_directory);
}

// Variables globales pour le prompt
extern int last_exit_status;

// Nouvelle fonction read_input avec prompt personnalisé
char* read_input() {
    rl_attempted_completion_function = las_completion;
    
    // UNIQUEMENT cette ligne, pas de test !
    char* prompt = generate_prompt();
    
    char* line = readline(prompt);
    return line;
}
int main(int argc, char** argv, char** env) {
    init_prompt_info();

    /*
     * FIX: command_setenv/unsetenv call free() on the env array when adding
     * new variables, but the original env passed by the OS is NOT heap-allocated.
     * Deep-copy it at startup so we always own it and can safely free/realloc.
     */
    int env_count = 0;
    while (env[env_count]) env_count++;
    char** my_env = malloc((env_count + 1) * sizeof(char*));
    if (!my_env) { perror("malloc"); return 1; }
    for (int i = 0; i < env_count; i++) my_env[i] = my_strdup(env[i]);
    my_env[env_count] = NULL;
    env = my_env;

    /* Load persistent trading environment from .trading_env */
    load_trading_env(&env);

    // Mode non-interactif pour les substitutions
    if (argc == 3 && strcmp(argv[1], "-c") == 0) {
        init_history();
        /* FIX: do NOT load aliases from disk in -c mode.
         * A stale .las_aliases (e.g. ls='aliased_ls' from a prior test run)
         * would corrupt every non-interactive invocation. -c mode should
         * always start with a clean alias state. */

        char* expanded  = expand_aliases(argv[2]);
        char* with_subs = process_line_with_substitutions(expanded);
        free(expanded);

        // FIX: capture and return the real exit status
        int status = execute_command_line(with_subs, env);
        free(with_subs);

        cleanup_history();
        cleanup_aliases();
        return status;
    }
    
    // Mode interactif normal
    if (argc > 1) {
        // Mode script
        init_history();
        /* FIX: do NOT load aliases from disk in script mode — same reason
         * as -c mode above. Scripts should be self-contained. */
        int script_status = execute_script(argv[1], env);
        cleanup_history();
        cleanup_aliases();
        cleanup_cd();
        return script_status;
    }
    
    printf("Welcome to LAS Shell!\n");
    shell_loop(env);
    cleanup_cd();
    return 0;
}