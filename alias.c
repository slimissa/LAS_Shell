#include "my_own_shell.h"

/* Alias typedef and extern declarations are in my_own_shell.h */
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
            return 0;
        }
    }
    
    // Ajouter nouveau
    if (alias_count < MAX_ALIASES) {
        aliases[alias_count].name = my_strdup(name);
        aliases[alias_count].value = my_strdup(value);
        alias_count++;
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
// FIX: loop until stable so chained aliases work (alias b='a'; alias a='echo hi')
char* expand_aliases(char* input) {
    if (!input || input[0] == '\0') return my_strdup(input);

    char* current = my_strdup(input);
    if (!current) return my_strdup(input);

    for (int iter = 0; iter < 10; iter++) {
        char* copy = my_strdup(current);
        if (!copy) break;

        /* isolate first word */
        char* first_word = copy;
        char* rest = copy;
        while (*rest && *rest != ' ' && *rest != '\t') rest++;
        if (*rest) {
            *rest = '\0';
            rest++;
            while (*rest == ' ' || *rest == '\t') rest++;
        }

        char* alias_value = find_alias(first_word);
        if (!alias_value) { free(copy); break; } /* no alias — done */

        /* guard against infinite self-referential alias */
        char av_buf[512];
        strncpy(av_buf, alias_value, sizeof(av_buf) - 1);
        av_buf[sizeof(av_buf) - 1] = '\0';
        char* av_first_end = av_buf;
        while (*av_first_end && *av_first_end != ' ' && *av_first_end != '\t')
            av_first_end++;
        *av_first_end = '\0';
        if (my_strcmp(av_buf, first_word) == 0) { free(copy); break; }

        size_t len = my_strlen(alias_value) + 1;
        if (rest && *rest) len += my_strlen(rest) + 1;

        char* result = malloc(len + 1);
        if (!result) { free(copy); break; }

        if (rest && *rest)
            sprintf(result, "%s %s", alias_value, rest);
        else
            sprintf(result, "%s", alias_value);

        free(copy);
        free(current);
        current = result;
    }

    return current;
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