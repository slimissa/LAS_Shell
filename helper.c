#include "my_own_shell.h"



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

    while (*start && strchr(delim, *start)) {

        start++;

    }



    if (*start == '\0') {

        *saveptr = start;

        return NULL;

    }



    char* end = start;

    while (*end && !strchr(delim, *end)) {

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