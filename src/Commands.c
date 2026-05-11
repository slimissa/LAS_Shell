#include "../include/my_own_shell.h"
#include "../include/risk_config.h"
#include <pwd.h>



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

    // Handle "~username" and "~username/path" - another user's home directory

    else if (args[1][0] == '~') {

        char* slash = my_strchr(args[1] + 1, '/');
        size_t ulen = slash ? (size_t)(slash - (args[1] + 1)) : strlen(args[1] + 1);
        char username[256];
        if (ulen == 0 || ulen >= sizeof(username)) {
            fprintf(stderr, "cd: invalid tilde expansion: %s\n", args[1]);
            return -1;
        }
        my_strncpy(username, args[1] + 1, ulen);
        username[ulen] = '\0';

        struct passwd* pw = getpwnam(username);
        if (!pw) {
            fprintf(stderr, "cd: unknown user: %s\n", username);
            return -1;
        }

        if (slash && *(slash + 1) != '\0') {
            size_t full_len = strlen(pw->pw_dir) + strlen(slash) + 1;
            target_dir = malloc(full_len);
            if (!target_dir) { perror("cd: malloc"); return -1; }
            snprintf(target_dir, full_len, "%s%s", pw->pw_dir, slash);
            allocated_target = 1;
        } else {
            target_dir = pw->pw_dir;
        }

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

        char* tok = args[i];

        /* \x01 prefix = single-quoted token — print literally, no expansion */
        if (tok[0] == '\x01') {
            printf("%s", tok + 1);   /* skip sentinel, print rest as-is */

        } else if (tok[0] == '$') {

            // Environment variable — unquoted or double-quoted $VAR token
            char* var_name = tok + 1; // Skip the '$'

            char* var_value = my_getenv(var_name, env);

            if (var_value != NULL) {

                printf("%s", var_value);

            }

        } else {

            printf("%s", tok);

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

    const char* built_in_commands[] = {
        "cd", "pwd", "echo", "env", "which", "exit",
        "setenv", "unsetenv",
        "jobs", "fg", "bg",
        "alias", "unalias", "source",
        "history",
        "setmarket", "setbroker", "setaccount", "setcapital",
        "assert", "watch", "work",
        "order", "positions", "balance", "cancel", "close_all",
        "reset_paper", "broker_status",
        "riskconfig", "audit", "checkpoint",
        NULL
    };

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

int command_exit(char** args) {
    int code = 0;
    if (args != NULL && args[1] != NULL)
        code = atoi(args[1]);
    printf("Exit shell\n");

    /* FIX BUG 5: 'exit' called exit() directly, bypassing the checkpoint
     * cleanup in main().  The background thread was never joined, and the
     * state file was never deleted, causing a phantom restore on the next
     * shell start even though this was a clean exit.
     *
     * Correct shutdown order:
     *   1. stop the checkpoint thread (join it)
     *   2. delete the state file      (signals clean exit to next startup)
     *   3. save history + aliases
     *   4. exit()
     */
    checkpoint_stop();
    checkpoint_delete();
    save_history();
    save_aliases();

    exit(code);
    return code;
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

//====================================================================
// trading env (las_shell)

/* ── Las_shell: trading environment ───────────────────────────── */

#define TRADING_ENV_FILE ".trading_env"

/* Write or update key=value in .trading_env */
void save_trading_env(const char* key, const char* value) {
    /* Read existing lines */
    char lines[64][512];
    int  count = 0;
    int  found = 0;

    FILE* f = fopen(TRADING_ENV_FILE, "r");
    if (f) {
        while (fgets(lines[count], sizeof(lines[count]), f) && count < 63) {
            lines[count][my_strcspn(lines[count], "\n")] = '\0';
            /* Check if this line is for our key */
            size_t klen = my_strlen(key);
            if (my_strncmp(lines[count], key, klen) == 0 && lines[count][klen] == '=') {
                snprintf(lines[count], sizeof(lines[count]), "%s=%s", key, value);
                found = 1;
            }
            count++;
        }
        fclose(f);
    }

    if (!found) {
        snprintf(lines[count], sizeof(lines[count]), "%s=%s", key, value);
        count++;
    }

    /* Rewrite the file */
    f = fopen(TRADING_ENV_FILE, "w");
    if (!f) { perror("save_trading_env"); return; }
    for (int i = 0; i < count; i++)
        fprintf(f, "%s\n", lines[i]);
    fclose(f);
}

/* Load .trading_env into the shell's env array at startup */
void load_trading_env(char*** env_ptr) {
    FILE* f = fopen(TRADING_ENV_FILE, "r");
    if (!f) return;   /* file doesn't exist yet — that's fine */

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[my_strcspn(line, "\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;

        /* Split on first '=' */
        char* eq = my_strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key   = line;
        char* value = eq + 1;

        /* Reuse command_setenv logic: build args[] and call it */
        char* args[4] = { "setenv", key, value, NULL };
        *env_ptr = command_setenv(args, *env_ptr);
    }
    fclose(f);
}

/* setmarket EXCHANGE */
char** command_setmarket(char** args, char** env) {
    if (!args[1]) {
        fprintf(stderr, "setmarket: usage: setmarket EXCHANGE\n");
        return env;
    }
    save_trading_env("MARKET", args[1]);
    char* sargs[4] = { "setenv", "MARKET", args[1], NULL };
    env = command_setenv(sargs, env);
    printf("MARKET=%s\n", args[1]);
    set_trading_env(env);   /* pass internal env[] to alias loader */
    setenv("MARKET", args[1], 1);  /* sync POSIX env for getenv() */
    reload_trading_aliases();
    return env;
}

/* setbroker BROKER */
char** command_setbroker(char** args, char** env) {
    if (!args[1]) {
        fprintf(stderr, "setbroker: usage: setbroker BROKER\n");
        return env;
    }
    save_trading_env("BROKER", args[1]);
    char* sargs[4] = { "setenv", "BROKER", args[1], NULL };
    env = command_setenv(sargs, env);
    printf("BROKER=%s\n", args[1]);
    return env;
}

/* setaccount MODE  (PAPER | LIVE) */
char** command_setaccount(char** args, char** env) {
    if (!args[1]) {
        fprintf(stderr, "setaccount: usage: setaccount PAPER|LIVE\n");
        return env;
    }
    save_trading_env("ACCOUNT", args[1]);
    char* sargs[4] = { "setenv", "ACCOUNT", args[1], NULL };
    env = command_setenv(sargs, env);
    printf("ACCOUNT=%s\n", args[1]);
    return env;
}

/* setcapital AMOUNT */
char** command_setcapital(char** args, char** env) {
    if (!args[1]) {
        fprintf(stderr, "setcapital: usage: setcapital AMOUNT\n");
        return env;
    }
    double cap = atof(args[1]);
    if (cap <= 0) {
        fprintf(stderr, "setcapital: amount must be a positive number\n");
        return env;
    }
    save_trading_env("CAPITAL", args[1]);
    char* sargs[4] = { "setenv", "CAPITAL", args[1], NULL };
    env = command_setenv(sargs, env);
    printf("CAPITAL=%s\n", args[1]);
    return env;
}
/* ── Las_shell: work mode toggle ──────────────────────────────────────
 * work       → enable trading prompt
 * work off   → disable, back to normal prompt               */
int command_work(char** args) {
    if (args[1] && my_strcmp(args[1], "off") == 0) {
        set_work_mode(0);
        printf("Trading prompt disabled.\n");
    } else {
        set_work_mode(1);
        printf("Trading prompt enabled. Welcome to work mode.\n");
    }
    return 0;
}

/* ── Las_shell: watch built-in ────────────────────────────────────────
 * Usage: watch <seconds> <command...>
 * Repeats command every N seconds until Ctrl+C.
 * ────────────────────────────────────────────────────────────── */
int command_watch(char** args, char** env) {
    if (!args[1] || !args[2]) {
        fprintf(stderr, "watch: usage: watch <seconds> <command...>\n");
        return 1;
    }

    int interval = atoi(args[1]);
    if (interval <= 0) {
        fprintf(stderr, "watch: interval must be a positive integer\n");
        return 1;
    }

    /* Rebuild the command string from args[2..] */
    char cmd[4096] = "";
    for (int i = 2; args[i]; i++) {
        if (i > 2) my_strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
        my_strncat(cmd, args[i], sizeof(cmd) - strlen(cmd) - 1);
    }

    /* Loop until SIGINT (Ctrl+C) sets watch_stop flag */
    while (!get_watch_stop()) {
        /* Clear screen and print header */
        printf("\033[2J\033[H");   /* clear screen, cursor home */
        printf("\001\033[1m\002watch %ds: %s\001\033[0m\002\n\n",
               interval, cmd);

        execute_command_line(cmd, env);

        /* Sleep in 100ms increments so Ctrl+C is responsive */
        for (int i = 0; i < interval * 10 && !get_watch_stop(); i++) {
            struct timespec ts = { 0, 100000000L }; /* 100ms */
            nanosleep(&ts, NULL);
        }
    }

    /* Reset flag for next watch call */
    set_watch_stop(0);
    printf("\nwatch: stopped\n");
    return 0;
}

/* ── Las_shell: assert built-in ────────────────────────────────────────
 * Usage: assert <left> <op> <right>
 * Numeric ops : <  <=  >  >=  ==  !=
 * String ops  : ==  !=
 * Returns 0 (success) or 1 (failure — script stops if used with &&)
 * ────────────────────────────────────────────────────────────── */
int command_assert(char** args, char** env) {
    /* Phase 4.3: zero-arg assert  ->  check ~/.las_shell_risk limits */
    if (!args[1]) {
        RiskResult rr;
        int rc = assert_risk_limits(env, &rr);
        if (rc != RISK_PASS) {
            risk_result_print(&rr);
            fprintf(stderr, "assert: risk limit breached: %s\n", rr.reason);
        }
        /* Normalize: any non-zero risk code → exit 1 (consistent with assert semantics) */
        return (rc != RISK_PASS) ? 1 : 0;
    }
    if (!args[2] || !args[3]) {
        fprintf(stderr, "assert: usage: assert <value> <op> <value>\n");
        return 1;
    }

    /* Reconstruct split operators: input_parser may split ">=" into ">" "=" */
    char* left_raw  = args[1];
    char* op_raw    = args[2];
    char* right_raw = args[3];
    char joined_op[3] = {0};
    if ((my_strcmp(op_raw, ">") == 0 || my_strcmp(op_raw, "<") == 0)
        && args[3] && my_strcmp(args[3], "=") == 0 && args[4]) {
        joined_op[0] = op_raw[0]; joined_op[1] = '=';
        op_raw    = joined_op;
        right_raw = args[4];
    }

    /* Resolve $VAR tokens */
    char* left  = (left_raw[0]  == '$') ? my_getenv(left_raw+1,  env) : left_raw;
    char* right = (right_raw[0] == '$') ? my_getenv(right_raw+1, env) : right_raw;
    if (!left)  left  = "";
    if (!right) right = "";

    char* op = op_raw;

    /* Detect numeric vs string */
    char* endL = NULL; char* endR = NULL;
    double L = strtod(left,  &endL);
    double R = strtod(right, &endR);
    int numeric = (endL && endR && *endL == '\0' && *endR == '\0' && left[0] != '\0' && right[0] != '\0');

    int result = 0;
    if (numeric) {
        if      (my_strcmp(op, "<" ) == 0) result = (L <  R);
        else if (my_strcmp(op, "<=") == 0) result = (L <= R);
        else if (my_strcmp(op, ">" ) == 0) result = (L >  R);
        else if (my_strcmp(op, ">=") == 0) result = (L >= R);
        else if (my_strcmp(op, "==") == 0) result = (L == R);
        else if (my_strcmp(op, "!=") == 0) result = (L != R);
        else {
            fprintf(stderr, "assert: unknown operator '%s'\n", op);
            return 1;
        }
    } else {
        if      (my_strcmp(op, "==") == 0) result = (my_strcmp(left, right) == 0);
        else if (my_strcmp(op, "!=") == 0) result = (my_strcmp(left, right) != 0);
        else {
            fprintf(stderr, "assert: operator '%s' not valid for strings\n", op);
            return 1;
        }
    }

    if (!result) {
        fprintf(stderr, "assert FAILED: %s %s %s\n", left, op, right);
        return 1;
    }
    return 0;
}
/* ══════════════════════════════════════════════════════════════════════════
 * command_audit() — Phase 4.1 built-in
 *
 * Sub-commands:
 *   audit verify [path]   replay chain hash, report any tampering
 *   audit show   [N]      pretty-print last N records (default 20)
 *   audit path            print current log file path
 *   audit status          print enabled/disabled + record count
 *   audit help            print usage
 *
 * Returns 0 on success, 1 on error or integrity violation.
 * ══════════════════════════════════════════════════════════════════════════ */
int command_audit(char **args, char **env) {
    (void)env;

    if (!args[1] || my_strcmp(args[1], "help") == 0) {
        printf(
            "audit — Las_shell compliance log management\n"
            "\n"
            "Usage:\n"
            "  audit verify [path]   Verify chain integrity of the audit log\n"
            "  audit show   [N]      Show last N records (default 20)\n"
            "  audit path            Print path of current audit log\n"
            "  audit status          Print audit mode status and record count\n"
            "  audit help            Show this help\n"
            "\n"
            "Enable audit mode by starting the shell with:\n"
            "  las_shell --audit [script.sh]\n"
            "  las_shell --audit --log /path/to/logfile [script.sh]\n"
            "\n"
            "Log format (CSV):\n"
            "  timestamp,username,pid,exit_code,stdout_sha256,chain_sha256,\"command\"\n"
            "\n"
            "Regulatory: SEC Rule 17a-4(f), MiFID II Art. 25\n"
        );
        return 0;
    }

    /* ── audit verify [path] ── */
    if (my_strcmp(args[1], "verify") == 0) {
        const char *path = args[2] ? args[2] : NULL;
        return audit_verify_log(path);
    }

    /* ── audit show [N] ── */
    if (my_strcmp(args[1], "show") == 0) {
        int n = 20;
        if (args[2]) {
            n = atoi(args[2]);
            if (n <= 0) n = 20;
        }
        audit_show_log(n);
        return 0;
    }

    /* -- audit path -- */
    if (my_strcmp(args[1], "path") == 0) {
        printf("%s\n", audit_get_path());
        return 0;
    }

    /* -- audit status -- */
    if (my_strcmp(args[1], "status") == 0) {
        if (audit_is_enabled()) {
            const char *path = audit_get_path();
            FILE *f = fopen(path, "r");
            int records = 0;
            if (f) {
                char line[256];
                while (fgets(line, sizeof(line), f))
                    if (line[0] != '#' && line[0] != '\n') records++;
                fclose(f);
            }
            printf("Audit mode: ENABLED\n");
            printf("Log file:   %s\n", path);
            printf("Records:    %d\n", records);
        } else {
            printf("Audit mode: DISABLED\n");
            printf("Start with: las_shell --audit [script.sh]\n");
        }
        return 0;
    }

    fprintf(stderr, "audit: unknown sub-command '%s' — try 'audit help'\n",
            args[1]);
    return 1;
}
