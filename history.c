#include "my_own_shell.h"

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
        "which", "exit", "jobs", "fg", "bg", "history", NULL
    };
    
    while ((name = (char*)builtins[list_index++])) {
        if (my_strncmp(name, text, len) == 0) {
            return my_strdup(name);
        }
    }
    
    // Chercher aussi dans PATH
    // (à implémenter plus tard)
    
    return NULL;
}

// Fonction de complétion
char** las_completion(const char* text, int start, int end) {
    (void)end;
    char** matches = NULL;
    
    // Ne compléter que si c'est le début de la ligne
    if (start == 0) {
        matches = rl_completion_matches(text, command_generator);
    }
    
    return matches;
}

void handle_sigint(int sig) {
    (void)sig;
    set_watch_stop(1);     /* stop any running watch loop */
    printf("\n");
    rl_on_new_line();
    rl_replace_line("", 0);
    rl_redisplay();
}