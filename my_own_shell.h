#define _POSIX_C_SOURCE 200809L



#include <stdio.h>

#include <stdlib.h>

#include <unistd.h>

#include <string.h>

#include <stddef.h>

#include <sys/types.h>

#include <sys/wait.h>

#include <fcntl.h> 

#include <sys/stat.h> 

#include <readline/readline.h>

#include <readline/history.h>

#include <signal.h>

/* ── Alias system ── */
typedef struct { char* name; char* value; } Alias;
extern Alias aliases[];
extern int   alias_count;


#include <time.h>


#define MAX_INPUT_SIZE 1024

#define MAX_PATH 4096

#define MAX_JOBS 100

#define HISTORY_FILE ".las_shell_history"

#define MAX_ALIASES 100


#define ALIAS_FILE ".las_aliases"

char* process_quotes(char* token);

char* strip_quotes(char* token);

char** parse_input(char* input);

void free_tokens(char** tokens);

typedef struct Command {

    char** args; // Command arguments

    char* output_file; // Output redirection file (NULL if none)

    int append_mode; // 1 for >>, 0 for >

} Command;

Command* parse_command_with_redirect(char* input);

void free_command(Command* cmd);

// redirection.c
int execute_with_redirect(char** args, char** env, char* output_file, int append_mode);

int execute_with_input_redirect(char** args, char** env, char* input_file);

int execute_with_both_redirect(char** args, char** env, char* input_file, char* output_file, int append_mode);

int execute_with_csv_log(char** args, char** env, const char* csv_file);
int execute_risk_gate(char** left_args, char** right_args, char** env);

// pipes.c
typedef struct {
    char*** commands;  // Tableau de commandes
    int cmd_count;      // Nombre de commandes
} Pipeline;

Pipeline* parse_pipeline(char* input);

int execute_pipeline(char*** commands, int cmd_count, char** env);

void free_pipeline(Pipeline* pipeline);

// operators.c
// Structure pour représenter une séquence de commandes avec opérateurs
typedef struct CommandNode {
    char** args;                // Arguments de la commande
    char* input_file;           // Redirection entrée
    char* output_file;          // Redirection sortie
    int append_mode;            // Mode append pour >>
    int background;             // 1 si commande en arrière-plan (&)
    struct CommandNode* next;   // Prochaine commande
    char* operator;             // "&&", "||", ";" ou NULL pour fin
} CommandNode;

// Structure pour gérer les jobs en arrière-plan
typedef struct {
    pid_t pid;
    char* command;
    int job_id;
    int active;  // 1 si en cours, 0 si terminé
} Job;

CommandNode* parse_operators(char* input);

char** split_on_operators(char* input, int* count);

int execute_sequence(CommandNode* head, char** env);

void free_sequence(CommandNode* head);

void add_job(pid_t pid, char* command);

void print_jobs();

void clean_jobs();

// Commandes internes pour la gestion des jobs
int command_jobs(char** args, char** env);
int command_fg(char** args, char** env);
int command_bg(char** args, char** env);

// history.c
void init_history(void);
void save_history(void);
void add_to_history(const char* command);
void cleanup_history(void);
char** las_completion(const char* text, int start, int end);
void handle_sigint(int sig);

// alias.c
void init_aliases(void);
void save_aliases(void);
char* expand_aliases(char* input);
// Commandes internes pour l'aliases
int command_alias(char** args);
int command_unalias(char** args);
void cleanup_aliases(void);
void load_trading_aliases(void);
void reload_trading_aliases(void);
void set_trading_env(char** env);

// script.c
int execute_script(char* filename, char** env);
int execute_command_line(char* input, char** env);
int execute_command_line_env(char* input, char*** env_ptr);
int command_source(char** args, char** env);

// substitution.c
char* process_line_with_substitutions(char* input);
char* expand_substitutions(const char* input);
char* expand_all_substitutions(const char* input);
char* execute_and_capture(const char* cmd);
char* find_next_substitution(const char* input, int* start_pos, int* end_pos);

// prompt.c
void init_prompt_info(void);
void update_exit_status(int status);
char* generate_prompt(void);

// main.c
void shell_loop(char** env);

int shell_builtins(char** args, char** env, char* initial_directory);

char* read_input(void);

// build in commands: cd, pwd, echo, env, setenv, unsetenv, which, exit

char* find_command_in_path(const char* command, char** env);

void cleanup_cd(void);



int command_cd(char** args, char* initial_directory, char** env);

int command_pwd();

int command_echo(char** args, char** env);

int command_env(char** env);

int command_which(char** args, char** env);

int command_exit(char** args);



char** command_setenv(char** args, char** env);

char** command_unsetenv(char** args, char** env);


int command_jobs(char** args, char** env);

int command_fg(char** args, char** env);

int command_bg(char** args, char** env);

//helper functions

int my_strcmp(const char* str1, const char* str2);

int my_strlen(const char* str);

int my_strncmp(const char* str1, const char* str2, size_t n);

char* my_strdup(const char* s);

char* my_strcpy(char* dest, const char* src);

char* my_strncpy(char* dest, const char* src, size_t n);

char* my_strchr(const char* str, int c);

char* my_strtok(char* str, const char* delim, char** saveptr);

size_t my_strcspn(const char* str, const char* accept);

char* my_strncat(char* dest, const char* src, size_t n);

char* my_strcat(char* dest, const char* src);

int my_strftime(char* buf, size_t buflen, const char* fmt, const struct tm* tm);

int   wait_until(const char* token);

char* my_getenv(const char* name, char** env);
char* expand_arg(const char* arg, char** env);
void  expand_args(char** args, char** env);
char* expand_vars_in_line(const char* input, char** env);
/* ── QShell: trading environment ── */
char** command_setmarket(char** args, char** env);
char** command_setbroker(char** args, char** env);
char** command_setaccount(char** args, char** env);
char** command_setcapital(char** args, char** env);
void   load_trading_env(char*** env_ptr);
void   save_trading_env(const char* key, const char* value);

/* ── QShell: work mode ── */
int    command_work(char** args);
void   set_work_mode(int on);
int    get_work_mode(void);

/* ── QShell: watch ── */
int  command_watch(char** args, char** env);
void set_watch_stop(int val);
int  get_watch_stop(void);

int command_assert(char** args, char** env);

int get_last_exit_status(void);