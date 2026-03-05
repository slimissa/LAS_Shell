#include "my_own_shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>

// Codes de couleurs ANSI - AVEC PROTECTION POUR READLINE
#define COLOR_RESET   "\001\033[0m\002"
#define COLOR_RED     "\001\033[31m\002"
#define COLOR_GREEN   "\001\033[32m\002"
#define COLOR_YELLOW  "\001\033[33m\002"
#define COLOR_BLUE    "\001\033[34m\002"
#define COLOR_MAGENTA "\001\033[35m\002"
#define COLOR_CYAN    "\001\033[36m\002"
#define COLOR_WHITE   "\001\033[37m\002"
#define COLOR_BOLD    "\001\033[1m\002"

// Variables globales pour le prompt
static int last_exit_status = 0;
static char* current_user = NULL;
static char hostname[64] = "";

// Initialiser les infos utilisateur/hôte
void init_prompt_info() {
    current_user = getenv("USER");
    if (!current_user) current_user = getenv("LOGNAME");
    if (!current_user) current_user = "user";
    
    if (gethostname(hostname, sizeof(hostname)) == -1) {
        strcpy(hostname, "localhost");
    } else {
        char* dot = strchr(hostname, '.');
        if (dot) *dot = '\0';
    }
}

void update_exit_status(int status) {
    last_exit_status = status;
}

// VERSION FINALE - PROPRESANS LES NOMS DE COULEURS
char* generate_prompt() {
    static char prompt[512];
    char cwd[256];
    char user_host[128];
    
    // Récupérer le répertoire courant
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        strcpy(cwd, "?");
    }
    
    // Remplacer $HOME par ~
    char* home = getenv("HOME");
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        char temp[256];
        snprintf(temp, sizeof(temp), "~%s", cwd + strlen(home));
        strcpy(cwd, temp);
    }
    
    // Format user@host
    snprintf(user_host, sizeof(user_host), "%s@%s", current_user, hostname);
    
    // Choisir la couleur selon le code de retour
    const char* status_color = (last_exit_status == 0) ? COLOR_GREEN : COLOR_RED;
    
    // Construction du prompt - SANS les noms de couleurs !
    snprintf(prompt, sizeof(prompt),
        "%s╭─%s%s%s %s%s%s\n"
        "%s╰─%s$ %s",
        COLOR_BOLD, 
        COLOR_GREEN, user_host, COLOR_RESET,
        COLOR_BLUE, cwd, COLOR_RESET,
        COLOR_BOLD, status_color, COLOR_RESET
    );
    
    return prompt;
}