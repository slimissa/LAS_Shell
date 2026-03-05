#include "my_own_shell.h"



//cd, cd[path], cd ~(home directory), cd .. (Replace [directory_name] with the name of the directory you want to enter (if it's within the current directory) or its full path. Move up one directory level.),cd -(previous directory) ,cd /(Change to the root directory), handle no existing directories, permissions issues.

// Global variable to track previous directory

static char* previous_directory = NULL;



int command_cd(char** args, char* initial_directory, char** env) {

    char* target_dir = NULL;

    char current_dir[1024];

    int allocated_target = 0; // Track if we allocated target_dir

    

    // Get current directory for "previous directory" functionality

    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {

        perror("cd: getcwd");

        return -1;

    }



    // No arguments - go to HOME directory

    if (args[1] == NULL) {

        target_dir = my_getenv("HOME", env);

        if (target_dir == NULL) {

            target_dir = initial_directory; // Fallback to initial directory

        }

    }

    // Handle "~" - home directory

    else if (my_strcmp(args[1], "~") == 0) {

        target_dir = my_getenv("HOME", env);

        if (target_dir == NULL) {

            fprintf(stderr, "cd: HOME environment variable not set\n");

            return -1;

        }

    }

    // Handle "~/" - home directory with relative path

    else if (my_strncmp(args[1], "~/", 2) == 0) {

        char* home = my_getenv("HOME", env);

        if (home == NULL) {

            fprintf(stderr, "cd: HOME environment variable not set\n");

            return -1;

        }

        // Combine HOME + the path after ~/

        size_t full_path_len = strlen(home) + strlen(args[1] + 1) + 1;

        target_dir = malloc(full_path_len);

        if (target_dir == NULL) {

            perror("cd: malloc");

            return -1;

        }

        snprintf(target_dir, full_path_len, "%s%s", home, args[1] + 1);

        allocated_target = 1;

    }

    // Handle "-" - previous directory

    else if (my_strcmp(args[1], "-") == 0) {

        if (previous_directory == NULL) {

            fprintf(stderr, "cd: no previous directory\n");

            return -1;

        }

        target_dir = previous_directory;

        printf("%s\n", target_dir); // Print the directory we're switching to

    }

    // Handle "/" - root directory

    else if (my_strcmp(args[1], "/") == 0) {  // Fixed: use my_strcmp

        target_dir = "/";

    }

    // Handle ".." - parent directory

    else if (my_strcmp(args[1], "..") == 0) {  // Fixed: use my_strcmp

        target_dir = args[1];

    }

    // Regular path

    else {

        target_dir = args[1];

    }



    // Change directory

    if (chdir(target_dir) != 0) {

        perror("cd");

        // Free allocated memory if we created the path for ~/ case

        if (allocated_target) {

            free(target_dir);

        }

        return -1;

    }



    // Update previous directory

    if (previous_directory != NULL) {

        free(previous_directory);

    }

    previous_directory = my_strdup(current_dir);

    if (previous_directory == NULL) {

        perror("cd: strdup");

        // Non-fatal error, continue

    }



    // Free allocated memory if we created the path for ~/ case

    if (allocated_target) {

        free(target_dir);

    }



    return 0;

}





int command_pwd(void) {

    char* cwd = getcwd(NULL, 0);

    

    if (cwd == NULL) {

        perror("pwd");

        return -1;

    }

    

    printf("%s\n", cwd);

    free(cwd);

    return 0;

}



//echo [-n] [string ...] (prints string to standard output followed by a newline character. If the -n option is provided, the trailing newline is omitted.), handle environment variables (e.g., echo $HOME should print the value of the HOME environment variable).

int command_echo(char** args, char** env) {

    int newline = 1; // By default, print newline

    int start_index = 1;



    if (args[1] != NULL && my_strcmp(args[1], "-n") == 0) {

        newline = 0; // Do not print newline

        start_index = 2; // Start printing from the next argument

    }



    for (size_t i = start_index; args[i] != NULL; i++) {

        if (args[i][0] == '$') {

            // Environment variable

            char* var_name = args[i] + 1; // Skip the '$'

            char* var_value = my_getenv(var_name, env);

            if (var_value != NULL) {

                printf("%s", var_value);

            }

        } else {

            printf("%s", args[i]);

        }

        if (args[i + 1] != NULL) {

            printf(" "); // Print space between arguments

        }

    }

    if (newline) {

        printf("\n");

    }

    return 0;

}



int command_env(char** env) {

    size_t i = 0;

    while (env[i]) {

        printf("%s\n", env[i]);

        i++;

    }   

    return 0;



}



char* find_command_in_path(const char* command, char** env);    

int command_which(char** args, char** env) {

    (void)env;

    if (args[1] == NULL) {

        printf("which: missing command name\n");

        return -1;

    }



    // Check for absolute/relative paths

    if (strchr(args[1], '/') != NULL) {

        if (access(args[1], X_OK) == 0) {

            printf("%s\n", args[1]);

            return 0;

        } else {

            printf("%s: not found\n", args[1]);

            return -1;

        }

    }



    // Check built-in commands

    const char* built_in_commands[] = {"cd", "pwd", "echo", "env", "which", "exit", NULL};

    for (int i = 0; built_in_commands[i] != NULL; i++) {

        if (my_strcmp(args[1], built_in_commands[i]) == 0) {

            printf("%s: shell built-in command\n", args[1]);

            return 0;

        }

    }



    // Search in PATH

    char* full_path = find_command_in_path(args[1], env);

    if (full_path != NULL) {

        printf("%s\n", full_path);

        free(full_path);

        return 0;

    } else {

        printf("%s: not found\n", args[1]);

        return -1;

    }

}



// Helper function to find command in PATH

char* find_command_in_path(const char* command, char** env) {

    // Get PATH environment variable

    char* path_env = my_getenv("PATH", env);

    if (path_env == NULL) {

        return NULL; // PATH not set

    }



    char* path_copy = my_strdup(path_env);

    if (path_copy == NULL) {

        return NULL; // Allocation failed

    }



    char* saveptr = NULL;

    char* dir = my_strtok(path_copy, ":", &saveptr);

    

    while (dir != NULL) {

        // Build full path: dir/command

        char full_path[MAX_PATH];

        snprintf(full_path, sizeof(full_path), "%s/%s", dir, command);

        

        // Check if file exists and is executable

        if (access(full_path, X_OK) == 0) {

            free(path_copy);

            return my_strdup(full_path);

        }

        

        dir = my_strtok(NULL, ":", &saveptr);

    }

    

    free(path_copy);

    return NULL; // Command not found in PATH

}



char** command_setenv(char** args, char** env) {

    if (args[1] == NULL || args[2] == NULL) {

        fprintf(stderr, "setenv: usage: setenv VARIABLE VALUE\n");        

        return env;

    }



    // Validate variable name

    if (strchr(args[1], '=') != NULL || args[1][0] == '\0') {

        fprintf(stderr, "setenv: invalid variable name '%s'\n", args[1]);

        return env;

    }



    // Construct new environment variable string

    size_t var_len = my_strlen(args[1]) + my_strlen(args[2]) + 2; // +2 for '=' and null terminator

    char* new_var = malloc(var_len);

    if (new_var == NULL) {

        perror("setenv");

        return env;

    }

    snprintf(new_var, var_len, "%s=%s", args[1], args[2]);



    // Check if variable already exists and replace it. also, count existing variables

    size_t i = 0;

    while (env[i] != NULL) {

        if (my_strncmp(env[i], args[1], my_strlen(args[1])) == 0 && env[i][my_strlen(args[1])] == '=') {

            // Replace existing variable

            free(env[i]);

            env[i] = new_var;

            return env;

        }

        i++;

    }



    // Variable does not exist, add new variable

    char** new_env = malloc((i + 2) * sizeof(char*)); // +2 for new var and NULL terminator

    if (new_env == NULL) {

        perror("setenv");

        free(new_var);

        return env;

    }



    // Copy existing variables

    for (size_t j = 0; j < i; j++) {

        new_env[j] = my_strdup(env[j]);



        // Check for allocation failure in strdup

        if (new_env[j] == NULL) {

            perror("setenv");

            // Free previously allocated memory

            for (size_t k = 0; k < j; k++) {

                free(new_env[k]);

            }

            free(new_env);

            free(new_var); 

            return env;

        }

    }

    new_env[i] = new_var;

    new_env[i + 1] = NULL;



    for (size_t i = 0; env[i]; i++) {

        free(env[i]);

    }

    free(env);

    return new_env;

}



char** command_unsetenv(char** args, char** env) {

    if (args[1] == NULL) {

        fprintf(stderr, "unsetenv: usage: unsetenv VARIABLE\n");  // Use stderr

        return env;

    }



    // Find the variable to remove

    size_t i = 0;

    size_t var_index = (size_t)-1;

    while (env[i] != NULL) {

        if (my_strncmp(env[i], args[1], my_strlen(args[1])) == 0 && env[i][my_strlen(args[1])] == '=') {

            var_index = i;

            break;

        }

        i++;

    }



    if (var_index == (size_t)-1) {

        // Variable not found

        return env;

    }



    // Create new environment array without the specified variable

    char** new_env = (char**)malloc(i * sizeof(char*)); // i instead of i+1 since one variable is removed

    if (new_env == NULL) {

        perror("unsetenv");

        return env;

    }



    size_t j = 0;

    for (size_t k = 0; k < i; k++) {

        if (k != var_index) {

            new_env[j++] = my_strdup(env[k]);

        } else {

            free(env[k]); // Free the removed variable

        }

    }

    new_env[j] = NULL;



    free(env);

    return new_env;

}

void cleanup_cd(void) {
    if (previous_directory != NULL) {
        free(previous_directory);
        previous_directory = NULL;
    }
}

int command_exit(void) {

    printf("Exit shell\n");

    exit(0);

    return 0;

}

// Commandes pour la gestion des jobs
extern Job jobs[MAX_JOBS];
extern int job_count;

int command_jobs(char** args, char** env) {
    (void)args;
    (void)env;
    clean_jobs();
    print_jobs();
    return 0;
}

int command_fg(char** args, char** env) {
    (void)env;
    if (args[1] == NULL) {
        fprintf(stderr, "fg: usage: fg %%job_id\n");
        return -1;
    }
    
    // Récupérer l'ID du job
    int job_id = 0;
    if (args[1][0] == '%') {
        job_id = atoi(args[1] + 1);
    } else {
        job_id = atoi(args[1]);
    }
    
    // Chercher le job
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].active && jobs[i].job_id == job_id) {
            printf("%s\n", jobs[i].command);
            // Attendre le processus
            int status;
            waitpid(jobs[i].pid, &status, 0);
            jobs[i].active = 0;
            free(jobs[i].command);
            return 0;
        }
    }
    
    fprintf(stderr, "fg: job %d not found\n", job_id);
    return -1;
}

int command_bg(char** args, char** env) {
    (void)env;
    if (args[1] == NULL) {
        fprintf(stderr, "bg: usage: bg %%job_id\n");
        return -1;
    }
    
    int job_id = atoi(args[1] + (args[1][0] == '%' ? 1 : 0));
    
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].active && jobs[i].job_id == job_id) {
            printf("[%d] %s\n", jobs[i].job_id, jobs[i].command);
            // Envoyer SIGCONT pour continuer le processus
            kill(jobs[i].pid, SIGCONT);
            return 0;
        }
    }
    
    fprintf(stderr, "bg: job %d not found\n", job_id);
    return -1;
}

// Commande source ou . (exécuter un script dans le shell courant)
int command_source(char** args, char** env) {
    if (args[1] == NULL) {
        fprintf(stderr, "source: usage: source filename\n");
        return -1;
    }
    
    // Exécuter le script dans le shell courant (pas de fork)
    extern int execute_script(char* filename, char** env);
    return execute_script(args[1], env);
}