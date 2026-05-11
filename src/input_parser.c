#include "../include/my_own_shell.h"

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
    if (input == NULL || my_strlen(input) == 0)
        return NULL;

    size_t  tok_cap = MAX_INPUT_SIZE;
    size_t  tok_cnt = 0;
    char**  tokens  = malloc(tok_cap * sizeof(char*));
    if (!tokens) { fprintf(stderr, "allocation error\n"); exit(EXIT_FAILURE); }

    /* build each token char-by-char into a dynamic buffer — avoids all
     * pointer-arithmetic bugs that plagued the original token_start approach */
    size_t  t_cap = 256;
    size_t  t_len = 0;
    char*   tbuf  = malloc(t_cap);
    if (!tbuf) { free(tokens); return NULL; }

/* append one char to token buffer, growing if needed */
#define TBUF_APPEND(c) do { \
    if (t_len + 1 >= t_cap) { t_cap *= 2; tbuf = realloc(tbuf, t_cap); } \
    tbuf[t_len++] = (c); \
} while(0)

/* flush current token buffer into tokens array */
#define FLUSH_TOKEN() do { \
    if (t_len > 0) { \
        tbuf[t_len] = '\0'; \
        if (tok_cnt + 1 >= tok_cap) { tok_cap *= 2; tokens = realloc(tokens, tok_cap * sizeof(char*)); } \
        tokens[tok_cnt++] = my_strdup(tbuf); \
        t_len = 0; \
    } \
} while(0)

    char* p = input;

    while (*p) {
        /* ---- single-quoted: everything literal, no expansion ---- */
        /* Supports the '\'' escape for embedding a literal single-quote:
         *   'foo'\''bar'  →  foo'bar
         * After closing ', if next chars are \'\' we emit ' and reopen. */
        if (*p == '\'') {
            p++;  /* skip opening quote */
            while (1) {
                while (*p && *p != '\'') { TBUF_APPEND(*p); p++; }
                if (!*p) break;          /* end of input inside quote */
                p++;                     /* skip closing ' */
                /* Check for \'' sequence: emit literal ' and reopen quote.
                 * The full 4-char pattern is: '  \  '  '
                 *                             ^close  ^emit ^reopen
                 * We are past the close '. Now at \. Skip \', emit ', skip reopen '. */
                if (*p == '\\' && *(p+1) == '\'') {
                    TBUF_APPEND('\'');  /* emit the literal single-quote */
                    p += 2;             /* skip the \' pair */
                    if (*p == '\'') p++;/* skip the reopen ' that starts next segment */
                    /* continue inner while — now processing next segment */
                } else {
                    break;              /* normal close quote */
                }
            }
            continue;
        }

        /* ---- double-quoted: $VAR expands, rest literal ---- */
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '$' && *(p+1) == '?') {
                    /* $? = last exit status */
                    char sbuf[16];
                    snprintf(sbuf, sizeof(sbuf), "%d", get_last_exit_status());
                    for (char* v = sbuf; *v; v++) TBUF_APPEND(*v);
                    p += 2;
                                } else if (*p == '$' && *(p+1) && *(p+1) != ' ') {
                    /* expand $VAR or ${VAR} inside double quotes */
                    p++;
                    if (*p == '{') {
                        /* Brace expansion: ${VAR} */
                        p++;  /* skip { */
                        char vname[256]; int vlen = 0;
                        while (*p && *p != '}' && vlen < 255)
                            vname[vlen++] = *p++;
                        vname[vlen] = '\0';
                        if (*p == '}') p++;  /* skip closing } */
                        if (vlen > 0) {
                            char* val = getenv(vname);
                            if (val) { for (char* v = val; *v; v++) TBUF_APPEND(*v); }
                        }
                    } else {
                        /* Bare $VAR expansion */
                        char vname[256]; int vlen = 0;
                        while (*p && ((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||
                                      (*p>='0'&&*p<='9')||*p=='_') && vlen<255)
                            vname[vlen++] = *p++;
                        vname[vlen] = '\0';
                        if (vlen > 0) {
                            char* val = getenv(vname);
                            if (val) { for (char* v = val; *v; v++) TBUF_APPEND(*v); }
                        } else {
                            TBUF_APPEND('$');
                        }
                    }
                } else {
                    TBUF_APPEND(*p); p++;
                }
            }
            if (*p == '"') p++;
            continue;
        }

        /* ---- whitespace: flush token ---- */
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            FLUSH_TOKEN();
            p++;
            continue;
        }

        /* ---- redirect operators emitted as own tokens ---- */
        /* ?> and |> are two-char operators — do NOT split them */
        if (*p == '>' && t_len > 0 && tbuf[t_len-1] == '?') {
            TBUF_APPEND(*p); p++;
            continue;
        }
        /* >= and <= are comparison operators — keep as single token */
        if ((*p == '>' || *p == '<') && *(p+1) == '=') {
            TBUF_APPEND(*p); p++;
            TBUF_APPEND(*p); p++;
            continue;
        }
        if (*p == '>' || *p == '<') {
            FLUSH_TOKEN();
            if (*p == '>' && *(p+1) == '>') {
                if (tok_cnt + 1 >= tok_cap) { tok_cap *= 2; tokens = realloc(tokens, tok_cap * sizeof(char*)); }
                tokens[tok_cnt++] = my_strdup(">>");
                p += 2;
            } else {
                char op[2] = {*p, '\0'};
                if (tok_cnt + 1 >= tok_cap) { tok_cap *= 2; tokens = realloc(tokens, tok_cap * sizeof(char*)); }
                tokens[tok_cnt++] = my_strdup(op);
                p++;
            }
            continue;
        }

        /* ---- $? outside quotes ---- */
        if (*p == '$' && *(p+1) == '?') {
            char sbuf[16];
            snprintf(sbuf, sizeof(sbuf), "%d", get_last_exit_status());
            for (char* v = sbuf; *v; v++) TBUF_APPEND(*v);
            p += 2;
            continue;
        }
        /* ---- ordinary character ---- */
        TBUF_APPEND(*p);
        p++;
    }

    FLUSH_TOKEN();

#undef TBUF_APPEND
#undef FLUSH_TOKEN

    free(tbuf);
    tokens[tok_cnt] = NULL;
    return tokens;
}