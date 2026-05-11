#include "../include/my_own_shell.h"

#define HISTORY_FILE ".las_shell_history"

// Initialiser l'historique
void init_history() {
    // Lire l'historique depuis le fichier
    read_history(HISTORY_FILE);
    
    // Limiter la taille de l'historique
    stifle_history(1000);
}

// Sauvegarder l'historique dans un fichier
void save_history() {
    write_history(HISTORY_FILE);
    history_truncate_file(HISTORY_FILE, 1000);
}

// Ajouter une commande à l'historique
void add_to_history(const char* command) {
    if (command && command[0] != '\0') {
        // Ne pas ajouter les commandes vides ou en double
        HIST_ENTRY *last = history_get(history_length);
        if (!last || strcmp(last->line, command) != 0) {
            add_history(command);
        }
    }
}

// Nettoyer l'historique
void cleanup_history() {
    save_history();
    clear_history();
}

// Fonction pour générer les complétions (TAB)
char* command_generator(const char* text, int state) {
    static int list_index, len;
    char* name;
    
    if (!state) {
        list_index = 0;
        len = strlen(text);
    }
    
    // Liste des commandes built-in
    const char* builtins[] = {
        "cd", "pwd", "echo", "env", "setenv", "unsetenv",
        "which", "exit", "jobs", "fg", "bg", "history",
        "alias", "unalias", "source",
        "setmarket", "setbroker", "setaccount", "setcapital",
        "assert", "watch", "work",
        "order", "positions", "balance", "cancel", "close_all",
        "reset_paper", "broker_status",
        "riskconfig", "audit", "checkpoint",
        "mstatus", "mpnl", "mopen", "mclose",
        "pnl", "pnl_log", "daily",
        "flatten", "risk", "check_risk",
        "gen_orders", "send_orders", "orders",
        "run_strat", "live", "paper", "backtest",
        "morning", "eod", "reset",
        "rejections", "audit", "clear_log",
        "watchlist", "add_watch",
        NULL
    };
    
    while ((name = (char*)builtins[list_index++])) {
        if (my_strncmp(name, text, len) == 0) {
            return my_strdup(name);
        }
    }
    
    // Chercher aussi dans PATH
    // (à implémenter plus tard)
    
        /* ── Search PATH for matching executables ── */
    if (text && text[0]) {
        static char* path_dirs[64];
        static int   path_idx = 0;
        static int   path_cnt = 0;
        
        if (!state) {
            path_cnt = 0;
            char* path_env = getenv("PATH");
            if (path_env) {
                char* pc = strdup(path_env);
                char* sp;
                char* d = strtok_r(pc, ":", &sp);
                while (d && path_cnt < 63) {
                    char fp[512];
                    snprintf(fp, sizeof(fp), "%s/%s", d, text);
                    if (access(fp, X_OK) == 0)
                        path_dirs[path_cnt++] = strdup(text);
                    d = strtok_r(NULL, ":", &sp);
                }
                free(pc);
                path_dirs[path_cnt] = NULL;
            }
            path_idx = 0;
        }
        if (path_idx < path_cnt)
            return strdup(path_dirs[path_idx++]);
    }
    
    return NULL;
}

// Fonction de complétion
char** las_completion(const char* text, int start, int end) {
    (void)end;
    char** matches = NULL;
    
    // Ne compléter que si c'est le début de la ligne
    if (start == 0) {
        matches = rl_completion_matches(text, command_generator);
    } else {
        matches = rl_completion_matches(text, rl_filename_completion_function);
    }
    
    return matches;
}

void handle_sigint(int sig) {
    (void)sig;
    set_watch_stop(1);     /* stop any running watch loop */
    stream_sub_close_all(); /* reap any live $<() streaming sources */
    printf("\n");
    rl_on_new_line();
    rl_replace_line("", 0);
    rl_redisplay();
}

/* ── SIGTERM handler — save checkpoint then exit cleanly ─────────────────
 * Called when the process receives SIGTERM (e.g. kill PID, system shutdown,
 * or a supervisor like systemd stopping the strategy runner).
 *
 * We cannot call checkpoint_save_now() with the live env here because
 * signal handlers must only call async-signal-safe functions, and fprintf /
 * malloc are not.  Instead we write a minimal "crash marker" via low-level
 * write(2) and let the checkpoint thread's last periodic save be the
 * recovery point.  The background thread already holds the last good state.
 * ────────────────────────────────────────────────────────────────────────*/
static volatile sig_atomic_t g_sigterm_received = 0;

void handle_sigterm(int sig) {
    (void)sig;
    g_sigterm_received = 1;
    /* Signal the checkpoint thread to wake and do one final save */
    /* pthread_cond_signal() is async-signal-safe on Linux (POSIX.1-2008) */
    /* We rely on the checkpoint thread noticing g_sigterm_received == 1   */
    /* via the external checkpoint_stop() call in shell_loop's exit path.  */
    /* The safest path: just set the flag; main loop checks it.            */
}

int get_sigterm_received(void) { return g_sigterm_received; }