#include "my_own_shell.h"

typedef struct {
    char* name;      // Nom de l'alias (ex: "ll")
    char* value;     // Valeur (ex: "ls -la")
} Alias;

Alias aliases[MAX_ALIASES];
int alias_count = 0;

// Initialiser les aliases depuis le fichier
void init_aliases() {
    FILE* f = fopen(ALIAS_FILE, "r");
    if (!f) return;
    
    char line[512];
    while (fgets(line, sizeof(line), f) && alias_count < MAX_ALIASES) {
        line[my_strcspn(line, "\n")] = 0;
        
        // Format: alias_name='valeur'
        char* equals = my_strchr(line, '=');
        if (equals) {
            *equals = '\0';
            char* name = line;
            char* value = equals + 1;
            
            // Enlever les guillemets autour de la valeur
            if (*value == '\'') {
                value++;
                value[my_strlen(value)-1] = '\0';
            }
            
            aliases[alias_count].name = my_strdup(name);
            aliases[alias_count].value = my_strdup(value);
            alias_count++;
        }
    }
    fclose(f);
}

// Sauvegarder les aliases dans le fichier
void save_aliases() {
    FILE* f = fopen(ALIAS_FILE, "w");
    if (!f) return;
    
    for (int i = 0; i < alias_count; i++) {
        fprintf(f, "%s='%s'\n", aliases[i].name, aliases[i].value);
    }
    fclose(f);
}

// Ajouter un alias
int add_alias(char* name, char* value) {
    // Vérifier si l'alias existe déjà
    for (int i = 0; i < alias_count; i++) {
        if (my_strcmp(aliases[i].name, name) == 0) {
            // Remplacer
            free(aliases[i].value);
            aliases[i].value = my_strdup(value);
            save_aliases();
            return 0;
        }
    }
    
    // Ajouter nouveau
    if (alias_count < MAX_ALIASES) {
        aliases[alias_count].name = my_strdup(name);
        aliases[alias_count].value = my_strdup(value);
        alias_count++;
        save_aliases();
        return 0;
    }
    return -1; // Trop d'aliases
}

// Supprimer un alias
int remove_alias(char* name) {
    for (int i = 0; i < alias_count; i++) {
        if (my_strcmp(aliases[i].name, name) == 0) {
            free(aliases[i].name);
            free(aliases[i].value);
            
            // Décaler les suivants
            for (int j = i; j < alias_count - 1; j++) {
                aliases[j] = aliases[j+1];
            }
            alias_count--;
            save_aliases();
            return 0;
        }
    }
    return -1; // Pas trouvé
}

// Lister tous les aliases
void list_aliases() {
    for (int i = 0; i < alias_count; i++) {
        printf("alias %s='%s'\n", aliases[i].name, aliases[i].value);
    }
}

// Trouver un alias par son nom
char* find_alias(char* name) {
    for (int i = 0; i < alias_count; i++) {
        if (my_strcmp(aliases[i].name, name) == 0) {
            return aliases[i].value;
        }
    }
    return NULL;
}

// Expander une ligne de commande (remplacer les aliases)
char* expand_aliases(char* input) {
    if (!input || input[0] == '\0') return my_strdup(input);
    
    // Copier la ligne pour la parser
    char* input_copy = my_strdup(input);
    if (!input_copy) return my_strdup(input);
    
    // Trouver le premier mot manuellement (sans strtok qui modifie la chaîne)
    char* first_word = input_copy;
    char* rest = input_copy;
    
    // Avancer jusqu'au premier espace ou tab
    while (*rest && *rest != ' ' && *rest != '\t') {
        rest++;
    }
    
    // Marquer la fin du premier mot
    if (*rest) {
        *rest = '\0';
        rest++;
        // Sauter les espaces supplémentaires
        while (*rest == ' ' || *rest == '\t') rest++;
    }
    
    // Chercher l'alias
    char* alias_value = find_alias(first_word);
    if (!alias_value) {
        free(input_copy);
        return my_strdup(input);
    }
    
    // Construire le résultat
    size_t len = my_strlen(alias_value) + 1;
    if (rest && *rest != '\0') {
        len += my_strlen(rest);
    }
    
    char* result = malloc(len + 1);
    if (!result) {
        free(input_copy);
        return my_strdup(input);
    }
    
    if (rest && *rest != '\0') {
        sprintf(result, "%s %s", alias_value, rest);
    } else {
        sprintf(result, "%s", alias_value);
    }
    
    free(input_copy);
    return result;
}
// Commande alias built-in

int command_alias(char** args) {
    if (args[1] == NULL) {
        list_aliases();
        return 0;
    }
    
    // Format: alias name='value'
    char* arg = args[1];
    char* equals = strchr(arg, '=');
    
    if (equals) {
        // alias x='y'
        *equals = '\0';
        char* name = arg;
        char* value = equals + 1;
        
        // Enlever les guillemets si présents
        if (*value == '\'' || *value == '"') {
            value++; // Avancer d'un caractère
            // Enlever le dernier guillemet
            int len = my_strlen(value);
            if (len > 0 && (value[len-1] == '\'' || value[len-1] == '"')) {
                value[len-1] = '\0';
            }
        }
        
        return add_alias(name, value);
    } else {
        char* val = find_alias(arg);
        if (val) {
            printf("alias %s='%s'\n", arg, val);
            return 0;
        } else {
            fprintf(stderr, "alias: %s not found\n", arg);
            return -1;
        }
    }
}

int command_unalias(char** args) {
    if (args[1] == NULL) {
        fprintf(stderr, "unalias: usage: unalias name\n");
        return -1;
    }
    
    return remove_alias(args[1]);
}


// Nettoyer la mémoire des aliases
void cleanup_aliases() {
    for (int i = 0; i < alias_count; i++) {
        free(aliases[i].name);
        free(aliases[i].value);
    }
    alias_count = 0;
}