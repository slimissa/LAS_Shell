#include "../include/my_own_shell.h"



int my_strcmp(const char* str1, const char* str2) {

    while (*str1 && (*str1 == *str2)) {

        str1++;

        str2++;

    }

    return *(const unsigned char*)str1 - *(const unsigned char*)str2;

}



int my_strlen(const char* str) {

    int length = 0;

    while (str[length] != '\0') {

        length++;

    }

    return length;

}



// Compares up to n characters of two strings

// Returns 0 if they are equal, a negative value if str1 < str2, and a positive value if str1 > str2

int my_strncmp(const char* str1, const char* str2, size_t n) {

    for (size_t i = 0; i < n; i++) {

        if (str1[i] != str2[i] || str1[i] == '\0' || str2[i] == '\0') {

            return (unsigned char)str1[i] - (unsigned char)str2[i];

        }

    }

    return 0;

}



char* my_strdup(const char* s) {

    if (s == NULL) return NULL;

    

    size_t len = my_strlen(s);

    char* dup = malloc(len + 1);

    if (dup == NULL) return NULL;

    

    my_strcpy(dup, s);

    return dup;

}



char* my_strcpy(char* dest, const char* src) {

    char* original_dest = dest;

    while ((*dest++ = *src++) != '\0');

    return original_dest;

}

char* my_strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char* my_strchr(const char* str, int c) {

    while (*str != '\0') {

        if (*str == (char)c) {

            return (char*)str;

        }

        str++;

    }

    if (c == '\0') {

        return (char*)str;

    }

    return NULL;

}



char* my_strtok(char* str, const char* delim, char** saveptr) {

    char* start;

    if (str) {
        start = str;
    } else {
        start = *saveptr;
    }

    // Skip leading delimiters
    while (*start && my_strchr(delim, *start)) {
        start++;
    }

    if (*start == '\0') {
        *saveptr = start;
        return NULL;
    }

    char* end = start;
    while (*end && !my_strchr(delim, *end)) {
        end++;
    }

    if (*end != '\0') {
        *end = '\0';
        *saveptr = end + 1;
    } else {
        *saveptr = end;
    }

    return start;
}


// Retrieves the value of an environment variable from the env array

char* my_getenv(const char* name, char** env) {

    if (name == NULL || env == NULL) {

        return NULL;

    }



    size_t name_len = my_strlen(name);

    for (int i = 0; env[i] != NULL; i++) {

        if (my_strncmp(env[i], name, name_len) == 0 && env[i][name_len] == '=') {

            return env[i] + name_len + 1; // Return value after '='

        }

    }

    return NULL; // Not found

}

size_t my_strcspn(const char* str, const char* reject) {

    size_t count = 0;

    while (str[count] != '\0') {

        if (my_strchr(reject, str[count]) != NULL) {

            return count;

        }

        count++;

    }

    return count;

}

char* my_strcat(char* dest, const char* src) {
    char* end = dest + my_strlen(dest);
    while (*src) { *end++ = *src++; }
    *end = '\0';
    return dest;
}

char* my_strncat(char* dest, const char* src, size_t n) {
    char* end = dest + my_strlen(dest);
    size_t i = 0;
    while (i < n && src[i]) { end[i] = src[i]; i++; }
    end[i] = '\0';
    return dest;
}

int my_strftime(char* buf, size_t buflen, const char* fmt, const struct tm* tm) {
    if (!buf || !fmt || !tm || buflen == 0) return 0;
 
    char* out = buf;
    size_t remaining = buflen - 1;   /* reserve space for null terminator */
 
    for (const char* p = fmt; *p && remaining > 0; p++) {
        if (*p != '%') {
            *out++ = *p;
            remaining--;
            continue;
        }
 
        p++;   /* skip '%' */
        char tmp[16];
        const char* s = tmp;
        int len = 0;
 
        switch (*p) {
            case 'Y':   /* 4-digit year */
                tmp[0] = '0' + (tm->tm_year + 1900) / 1000;
                tmp[1] = '0' + ((tm->tm_year + 1900) / 100) % 10;
                tmp[2] = '0' + ((tm->tm_year + 1900) / 10) % 10;
                tmp[3] = '0' + (tm->tm_year + 1900) % 10;
                len = 4; break;
            case 'm':   /* month 01-12 */
                tmp[0] = '0' + (tm->tm_mon + 1) / 10;
                tmp[1] = '0' + (tm->tm_mon + 1) % 10;
                len = 2; break;
            case 'd':   /* day 01-31 */
                tmp[0] = '0' + tm->tm_mday / 10;
                tmp[1] = '0' + tm->tm_mday % 10;
                len = 2; break;
            case 'H':   /* hour 00-23 */
                tmp[0] = '0' + tm->tm_hour / 10;
                tmp[1] = '0' + tm->tm_hour % 10;
                len = 2; break;
            case 'M':   /* minute 00-59 */
                tmp[0] = '0' + tm->tm_min / 10;
                tmp[1] = '0' + tm->tm_min % 10;
                len = 2; break;
            case 'S':   /* second 00-60 */
                tmp[0] = '0' + tm->tm_sec / 10;
                tmp[1] = '0' + tm->tm_sec % 10;
                len = 2; break;
            case 'T':   /* %H:%M:%S */
                tmp[0] = '0' + tm->tm_hour / 10;
                tmp[1] = '0' + tm->tm_hour % 10;
                tmp[2] = ':';
                tmp[3] = '0' + tm->tm_min / 10;
                tmp[4] = '0' + tm->tm_min % 10;
                tmp[5] = ':';
                tmp[6] = '0' + tm->tm_sec / 10;
                tmp[7] = '0' + tm->tm_sec % 10;
                len = 8; break;
            case 'F':   /* %Y-%m-%d */
                tmp[0] = '0' + (tm->tm_year + 1900) / 1000;
                tmp[1] = '0' + ((tm->tm_year + 1900) / 100) % 10;
                tmp[2] = '0' + ((tm->tm_year + 1900) / 10) % 10;
                tmp[3] = '0' + (tm->tm_year + 1900) % 10;
                tmp[4] = '-';
                tmp[5] = '0' + (tm->tm_mon + 1) / 10;
                tmp[6] = '0' + (tm->tm_mon + 1) % 10;
                tmp[7] = '-';
                tmp[8] = '0' + tm->tm_mday / 10;
                tmp[9] = '0' + tm->tm_mday % 10;
                len = 10; break;
            case '%':
                tmp[0] = '%'; len = 1; break;
            default:
                tmp[0] = '%'; tmp[1] = *p; len = 2; break;
        }
 
        /* copy tmp into out */
        for (int i = 0; i < len && remaining > 0; i++) {
            *out++ = s[i];
            remaining--;
        }
    }
 
    *out = '\0';
    return (int)(out - buf);
}
/* ── expand_arg(): expand ~ and $VAR in a single token ── */
char* expand_arg(const char* arg, char** env) {
    if (!arg) return NULL;
    /* tilde */
    if (arg[0] == '~' && (arg[1] == '/' || arg[1] == '\0')) {
        char* home = my_getenv("HOME", env);
        if (!home) home = getenv("HOME");
        if (home) {
            size_t len = my_strlen(home) + my_strlen(arg + 1) + 1;
            char* r = malloc(len + 1);
            if (r) { my_strcpy(r, home); my_strcat(r, arg + 1); return r; }
        }
        return my_strdup(arg);
    }
    /* $VAR — whole token */
    if (arg[0] == '$' && arg[1] != '\0' && arg[1] != '?') {
        const char* vn = arg + 1;
        int ok = 1;
        for (int i = 0; vn[i]; i++) {
            char c = vn[i];
            if (!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))
                { ok = 0; break; }
        }
        if (ok) {
            char* val = my_getenv(vn, env);
            if (!val) val = getenv(vn);
            return my_strdup(val ? val : "");
        }
    }
    return my_strdup(arg);
}

/* ── expand_args(): expand all tokens in-place ── */
void expand_args(char** args, char** env) {
    if (!args || !env) return;
    for (int i = 0; args[i]; i++) {
        char* ex = expand_arg(args[i], env);
        if (ex && ex != args[i]) { free(args[i]); args[i] = ex; }
    }
}

/* ── expand_vars_in_line(): expand $VAR in raw input line ──────────
 * Used BEFORE operator detection so |> $LOGFILE works.
 * Only expands standalone $VARNAME tokens (space-delimited).
 * Preserves quoted strings unchanged.
 * Returns a malloc'd string.
 * ─────────────────────────────────────────────────────────────────*/
char* expand_vars_in_line(const char* input, char** env) {
    if (!input) return NULL;
    /* result buffer — grows as needed */
    size_t cap = 1024, len = 0;
    char* out = malloc(cap);
    if (!out) return my_strdup(input);

    const char* p = input;
    while (*p) {
        /* skip single-quoted sections verbatim */
        if (*p == '\'') {
            out[len++] = *p++;
            while (*p && *p != '\'') {
                if (len + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
                out[len++] = *p++;
            }
            if (*p) { if (len + 2 >= cap) { cap *= 2; out = realloc(out, cap); } out[len++] = *p++; }
            continue;
        }
        /* double-quoted sections: copy literally but DO expand $VAR inside */
        if (*p == '"') {
            if (len + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
            out[len++] = *p++;          /* opening " */
            while (*p && *p != '"') {
                if (*p == '\\') {     /* backslash escape inside "" */
                    if (len + 4 >= cap) { cap *= 2; out = realloc(out, cap); }
                    out[len++] = *p++;
                    if (*p) out[len++] = *p++;
                    continue;
                }
                if (*p == '$' && *(p+1) && *(p+1) != ' ' && *(p+1) != '\0') {
                    p++;
                    char vname2[256]; int vlen2 = 0;
                    int braced2 = (*p == '{');
                    if (braced2) p++;   /* skip '{' */
                    while (*p && ((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||
                                  (*p>='0'&&*p<='9')||*p=='_') && vlen2<255)
                        vname2[vlen2++] = *p++;
                    vname2[vlen2] = '\0';
                    
                    /* Check for :- modifier: ${VAR:-default} */
                    char *defval2 = NULL;
                    char defbuf2[256] = {0};
                    if (braced2 && *p == ':' && *(p+1) == '-') {
                        p += 2;  /* skip :- */
                        int dlen = 0;
                        while (*p && *p != '}' && dlen < 255)
                            defbuf2[dlen++] = *p++;
                        defbuf2[dlen] = '\0';
                        defval2 = defbuf2;
                    }
                    
                    if (braced2 && *p == '}') p++;  /* skip '}' */
                    if (vlen2 > 0) {
                        char* val2 = my_getenv(vname2, env);
                        if (!val2) val2 = getenv(vname2);
                        if (!val2 || val2[0] == '\0') val2 = defval2;
                        if (val2) {
                            size_t vl2 = my_strlen(val2);
                            while (len + vl2 + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
                            my_strcpy(out + len, val2); len += vl2;
                        }
                    } else {
                        if (len + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
                        out[len++] = '$';
                    }
                    continue;
                }
                if (len + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
                out[len++] = *p++;
            }
            if (*p) { if (len + 2 >= cap) { cap *= 2; out = realloc(out, cap); } out[len++] = *p++; }
            continue;
        }
        /* tilde: ~ at start or after space */
        if (*p == '~' && (p == input || *(p-1) == ' ') && (*(p+1) == '/' || *(p+1) == ' ' || *(p+1) == '\0')) {
            char* home = my_getenv("HOME", env);
            if (!home) home = getenv("HOME");
            if (home) {
                size_t hl = my_strlen(home);
                while (len + hl + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
                my_strcpy(out + len, home); len += hl;
                p++;
                continue;
            }
        }
        /* ${VAR} and $VAR */
        if (*p == '$' && *(p+1) && *(p+1) != ' ' && *(p+1) != '\0') {
            p++;
            char vname[256]; int vlen = 0;
            int braced = (*p == '{');
            if (braced) p++;   /* skip '{' */
            while (*p && ((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||
                          (*p>='0'&&*p<='9')||*p=='_') && vlen<255)
                vname[vlen++] = *p++;
            vname[vlen] = '\0';
            
            /* Check for :- modifier: ${VAR:-default} */
            char *defval = NULL;
            char defbuf[256] = {0};
            if (braced && *p == ':' && *(p+1) == '-') {
                p += 2;  /* skip :- */
                int dlen = 0;
                while (*p && *p != '}' && dlen < 255)
                    defbuf[dlen++] = *p++;
                defbuf[dlen] = '\0';
                defval = defbuf;
            }
            
            if (braced && *p == '}') p++;  /* skip '}' */
            if (vlen > 0) {
                char* val = my_getenv(vname, env);
                if (!val) val = getenv(vname);
                if (!val || val[0] == '\0') val = defval;  /* use default if unset/empty */
                if (val) {
                    size_t vl = my_strlen(val);
                    while (len + vl + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
                    my_strcpy(out + len, val); len += vl;
                } /* undefined + no default → empty */
            } else {
                if (len + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
                out[len++] = '$';
            }
            continue;
        }
        if (len + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
        out[len++] = *p++;
    }
    out[len] = '\0';
    return out;
}