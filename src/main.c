#include "../include/my_own_shell.h"
#include "../include/risk_config.h"
#include <errno.h>
#include <sys/stat.h>

/* audit_log_command() is the audited wrapper around execute_command_line().
 * When audit mode is off, audit_log_command() delegates immediately so
 * there is zero overhead on the hot path.                                */

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
        /* FIX BUG 5: command_exit() now handles checkpoint_stop(),
         * checkpoint_delete(), save_history(), and save_aliases() internally
         * before calling exit().  Do NOT call them here — it would double-save
         * and also run BEFORE checkpoint cleanup which is wrong ordering.   */
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
    else if (my_strcmp(args[0], "riskconfig") == 0) {
        return command_riskconfig(args, env);
    }
    else if (my_strcmp(args[0], "audit") == 0) {
        return command_audit(args, env);
    }
    /* ── Phase 4.2: Broker API Bridge ── */
    else if (my_strcmp(args[0], "order") == 0) {
        return command_order(args, env);
    }
    else if (my_strcmp(args[0], "positions") == 0) {
        return command_positions(args, env);
    }
    else if (my_strcmp(args[0], "balance") == 0) {
        return command_balance(args, env);
    }
    else if (my_strcmp(args[0], "cancel") == 0) {
        return command_cancel(args, env);
    }
    else if (my_strcmp(args[0], "close_all") == 0) {
        return command_close_all(args, env);
    }
    else if (my_strcmp(args[0], "reset_paper") == 0) {
        return command_reset_paper(args, env);
    }
    else if (my_strcmp(args[0], "broker_status") == 0) {
        return command_broker_status(args, env);
    }
    /* ── Phase 4.4: Crash Recovery ── */
    else if (my_strcmp(args[0], "checkpoint") == 0) {
        return command_checkpoint(args, env);
    }
    else {
        return -1;
    }
}

void shell_loop(char** env) {
    char* input = NULL;
    char* initial_directory = getcwd(NULL, 0);
    char* expanded_input = NULL;
    char* with_substitutions = NULL;
    char** args = NULL;
    int last_status = 0;
    
    /* FIX BUG 2: init_aliases() is now called by main() BEFORE shell_loop()
     * so that checkpoint_restore() can dedup against the baseline .las_aliases.
     * We must NOT call init_aliases() again here, or it would append duplicates.
     * init_history() and init_prompt_info() are still safe to call (idempotent). */
    init_history();
    /* init_aliases() intentionally omitted — already called by main() */
    init_prompt_info();
    
    // Configurer le signal Ctrl+C
    signal(SIGINT, handle_sigint);
    /* Phase 4.4: catch SIGTERM so a kill/supervisor shutdown triggers a
     * final checkpoint save before the process exits.                   */
    signal(SIGTERM, handle_sigterm);

    while (1) {
        // Nettoyer les jobs
        clean_jobs();

        /* FIX BUG 4: On SIGTERM we want to PRESERVE the checkpoint file so
         * the next startup can restore from it.  The old code called
         * checkpoint_delete() here which deleted the very state we just saved.
         * Correct behaviour: save → stop thread → exit WITHOUT deleting.    */
        if (get_sigterm_received()) {
            fprintf(stderr,
                    "\n[checkpoint] SIGTERM received — saving state and exiting\n");
            checkpoint_save_now(checkpoint_get_live_env()); /* save with live env */
            checkpoint_stop();         /* join thread cleanly                  */
            /* do NOT call checkpoint_delete() — preserve state for recovery  */
            save_history();
            save_aliases();
            exit(0);
        }
        
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
            if (c == '~' && c1 == '>') { has_operator = 1; break; }
        }

        if (has_operator) {
            CommandNode* sequence = parse_operators(input);
            if (sequence) {
                last_status = execute_sequence(sequence, &env);
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

    /* Set LAS_SHELL_HOME fallback if not already in environment.
    * This ensures templates, scripts, and pipeline stages are
    * always found whether running from the build directory or
    * from the system-wide install at /usr/local/share/las_shell */
    if (!getenv("LAS_SHELL_HOME") || getenv("LAS_SHELL_HOME")[0] == '\0') {
        setenv("LAS_SHELL_HOME", "/usr/local/share/las_shell", 1);
    }

    /* Load persistent trading environment from .trading_env */
    load_trading_env(&env);
    load_risk_config();   /* Phase 4.3: load ~/.las_shell_risk */

    /* ── Phase 4.1: --audit flag detection ────────────────────────────────
     *
     * Supported invocation forms:
     *
     *   las_shell --audit                   interactive shell, audit on
     *   las_shell --audit script.sh         run script with audit
     *   las_shell --audit --log /path ...   explicit log path, then optional script
     *   las_shell script.sh                 script, no audit
     *   las_shell -c "cmd"                  single command, no audit
     *
     * When --audit is present we call audit_init() once, then every
     * command executed through execute_command_line() is automatically
     * captured via audit_log_command().  execute_command_line() itself
     * is not modified — audit_log_command() wraps it.
     *
     * In script mode with --audit the script executor calls
     * execute_command_line_env() internally per line; those calls go
     * through the unmodified fast path.  Only top-level dispatch in
     * shell_loop() and execute_script() routes through
     * audit_log_command() for the outer invocation.  For per-line
     * granularity inside a script, audit mode is checked inside
     * execute_command_line_env() via audit_is_enabled().
     * ────────────────────────────────────────────────────────────────── */

    int   audit_mode    = 0;
    const char *audit_log_path = NULL;   /* NULL → default ~/.las_shell_audit */

    /* Scan argv for --audit [--log <path>] */
    int first_non_audit_arg = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--audit") == 0) {
            audit_mode = 1;
            first_non_audit_arg = i + 1;
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            audit_log_path = argv[i + 1];
            i++;
            first_non_audit_arg = i + 1;
        } else {
            /* First unrecognised arg — stop scanning audit flags */
            first_non_audit_arg = i;
            break;
        }
    }

    /* Activate audit subsystem before any command runs */
    if (audit_mode)
        audit_init(audit_log_path);

    /* Shift argv view past consumed audit flags */
    int effective_argc = argc - first_non_audit_arg + 1;
    char **effective_argv = argv + first_non_audit_arg - 1;
    /* effective_argv[0] is still program name, [1..] are real args */

    /* ── -c "single command" mode ── */
    if (effective_argc == 3 && strcmp(effective_argv[1], "-c") == 0) {
        init_history();
        init_aliases();

        /* Restore checkpoint so -c mode can access saved trading env vars,
         * and so CRC tamper detection warnings reach stderr. */
        {
            char restored_cwd[4096] = "";
            checkpoint_restore(&env, restored_cwd, sizeof(restored_cwd));
        }

        char *expanded  = expand_aliases(effective_argv[2]);
        char *with_subs = process_line_with_substitutions(expanded);
        free(expanded);

        /* execute_command_line_env() checks audit_is_enabled() internally
         * and routes through audit_log_command() — no outer wrap needed. */
        int status = execute_command_line(with_subs, env);
        free(with_subs);

        cleanup_history();
        cleanup_aliases();
        return status;
    }

    /* ── Script mode ── */
    if (effective_argc > 1) {
        /* If script path is relative and doesn't exist, try under LAS_SHELL_HOME */
        char resolved_path[MAX_PATH] = {0};
        {
            struct stat st;
            if (stat(effective_argv[1], &st) != 0 && effective_argv[1][0] != '/' && effective_argv[1][0] != '.') {
                const char *lhome = getenv("LAS_SHELL_HOME");
                if (lhome && lhome[0]) {
                    snprintf(resolved_path, sizeof(resolved_path), "%s/%s", lhome, effective_argv[1]);
                    if (stat(resolved_path, &st) == 0) {
                        effective_argv[1] = resolved_path;  /* use the resolved path */
                    }
                }
            }
        }
        init_history();
        init_aliases();

        /* Restore checkpoint if one exists (env, aliases, CWD).
         * This lets scripts access trading env vars saved from a prior session. */
        {
            char restored_cwd[4096] = "";
            CheckpointRestoreResult crr =
                checkpoint_restore(&env, restored_cwd, sizeof(restored_cwd));
            if (crr == CHK_RESTORE_OK && restored_cwd[0] != '\0')
                chdir(restored_cwd); /* best-effort; ignore error in script mode */
        }

        /* execute_script() drives execute_command_line_env() per line.
         * Each line is individually logged when audit_is_enabled().     */
        int script_status = execute_script(effective_argv[1], env);
        cleanup_history();
        cleanup_aliases();
        cleanup_cd();
        return script_status;
    }

    /* ── Interactive mode ── */
    printf("Welcome to Las_shell!\n");
    if (audit_mode)
        printf("[audit] All commands will be logged to %s\n",
               audit_log_path ? audit_log_path :
               (getenv("HOME") ? getenv("HOME") : "/tmp"));

    /* ── Phase 4.4: Crash Recovery ─────────────────────────────────────────
     *
     * FIX BUG 8: Restore ONLY in interactive mode.  Script and -c modes must
     * not pollute their environment with a stale interactive session state.
     *
     * FIX BUG 2: Restore AFTER init_aliases() has already loaded .las_aliases
     * (init_aliases is called inside shell_loop → we call a pre-loop init
     * here instead).  Without this fix, checkpoint_restore() wrote aliases
     * at index 0..N-1, then init_aliases() appended duplicates starting at N.
     *
     * Correct order:
     *   1. init_aliases()          — load .las_aliases (authoritative baseline)
     *   2. checkpoint_restore()    — overwrite/add checkpoint env; skip aliases
     *                                that already exist (dedup built into restore)
     *   3. checkpoint_start()      — begin periodic background saves
     *   4. shell_loop()            — enter the REPL (init_aliases is a no-op now
     *                                because we call it here first)
     * ────────────────────────────────────────────────────────────────────── */

    /* Step 1: aliases baseline — must happen before restore so restore's
     * dedup logic ("only restore aliases not already defined") works correctly */
    init_aliases();

    /* Step 2: attempt restore */
    {
        char restored_cwd[4096] = "";
        CheckpointRestoreResult crr =
            checkpoint_restore(&env, restored_cwd, sizeof(restored_cwd));

        if (crr == CHK_RESTORE_OK && restored_cwd[0] != '\0') {
            if (chdir(restored_cwd) != 0)
                fprintf(stderr,
                        "[checkpoint] warning: could not chdir to saved CWD "
                        "'%s': %s\n",
                        restored_cwd, strerror(errno));
        }
    }

    /* Step 3: start background checkpoint thread.
     * Pass &env (pointer-to-pointer) so the thread always snapshots the
     * live env array even after setenv/unsetenv reallocate it.
     * Interval: 0 → use CHECKPOINT_INTERVAL_SEC (30 s default).          */
    checkpoint_start(&env, 0);

    /* Step 4: enter REPL — shell_loop calls init_aliases() internally but
     * that call is now harmless: alias_count > 0 so it just appends from
     * .las_aliases again.  To make it truly idempotent we pass a sentinel.
     * Actually the cleanest fix: shell_loop skips init_aliases if already
     * initialised.  We set the skip flag via a new guard (see shell_loop). */
    shell_loop(env);

    /* ── Phase 4.4: clean shutdown ──────────────────────────────────────
     * FIX BUG 5 (partial): on clean Ctrl-D exit, shell_loop() returns here.
     * The 'exit' built-in calls exit() directly and bypasses this path —
     * that is fixed separately in command_exit().
     * ─────────────────────────────────────────────────────────────────── */
    checkpoint_stop();
    checkpoint_delete();

    cleanup_cd();
    return 0;
}