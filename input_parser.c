#include "my_own_shell.h"

void free_tokens(char** tokens) {
    if (tokens == NULL) return;
    
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
}

// Fonction pour enlever les guillemets autour d'un token
char* strip_quotes(char* token) {
    if (!token) return NULL;
    
    int len = my_strlen(token);
    
    // Vérifier si le token est entouré de guillemets (simples ou doubles)
    if (len >= 2 && ((token[0] == '"' && token[len-1] == '"') || 
                     (token[0] == '\'' && token[len-1] == '\''))) {
        // Enlever les guillemets au début et à la fin
        char* result = malloc(len - 1);
        if (!result) return my_strdup(token);
        
        my_strncpy(result, token + 1, len - 2);
        result[len-2] = '\0';
        return result;
    }
    
    // Pas de guillemets, retourner une copie du token original
    return my_strdup(token);
}

char** parse_input(char* input) {
    if (input == NULL || my_strlen(input) == 0) {
        return NULL;
    }

    size_t buffer_size = MAX_INPUT_SIZE;
    size_t position = 0;
    char** tokens = malloc(buffer_size * sizeof(char*));

    if (!tokens) {
        fprintf(stderr, "allocation error\n");
        exit(EXIT_FAILURE);
    }

    char* input_copy = my_strdup(input);
    if (!input_copy) {
        fprintf(stderr, "allocation error\n");
        free(tokens);
        return NULL;
    }

    int in_quotes = 0;
    char quote_char = 0;
    char* current = input_copy;
    char* token_start = current;
    int token_len = 0;

    while (*current) {
        // Gestion des guillemets
        if (*current == '"' || *current == '\'') {
            if (!in_quotes) {
                in_quotes = 1;
                quote_char = *current;
                // Ne pas inclure le guillemet dans le token
                if (token_len == 0) {
                    token_start = current + 1;
                } else {
                    token_len++;
                }
                current++;
                continue;
            } else if (*current == quote_char) {
                in_quotes = 0;
                quote_char = 0;
                // Ne pas inclure le guillemet dans le token
                current++;
                continue;
            }
        }

        // Si on n'est pas entre guillemets et qu'on rencontre un espace
        if (!in_quotes && (*current == ' ' || *current == '\t' || *current == '\n' || *current == '\r')) {
            if (token_len > 0) {
                // Extraire le token
                char* token = malloc(token_len + 1);
                if (!token) {
                    for (size_t i = 0; i < position; i++) free(tokens[i]);
                    free(tokens);
                    free(input_copy);
                    return NULL;
                }
                
                strncpy(token, token_start, token_len);
                token[token_len] = '\0';
                tokens[position++] = token;

                // Redimensionner si nécessaire
                if (position >= buffer_size) {
                    buffer_size *= 2;
                    char** new_tokens = realloc(tokens, buffer_size * sizeof(char*));
                    if (!new_tokens) {
                        for (size_t i = 0; i < position; i++) free(tokens[i]);
                        free(tokens);
                        free(input_copy);
                        return NULL;
                    }
                    tokens = new_tokens;
                }
                
                token_len = 0;
            }
            token_start = current + 1;
        } else {
            token_len++;
        }
        current++;
    }

    // Dernier token
    if (token_len > 0) {
        char* token = malloc(token_len + 1);
        if (!token) {
            for (size_t i = 0; i < position; i++) free(tokens[i]);
            free(tokens);
            free(input_copy);
            return NULL;
        }
        
        strncpy(token, token_start, token_len);
        token[token_len] = '\0';
        tokens[position++] = token;
    }

    tokens[position] = NULL;
    free(input_copy);
    
    return tokens;
}