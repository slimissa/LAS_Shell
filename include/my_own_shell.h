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

#include <time.h>

/* ── Alias system ── */
typedef struct { char* name; char* value; } Alias;
extern Alias aliases[];
extern int   alias_count;

#include "risk_config.h"
#include "crash_recovery.h"
#include "broker.h"


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

int execute_sequence(CommandNode* head, char*** env_ptr);

void free_sequence(CommandNode* head);

void add_job(pid_t pid, char* command);

void print_jobs();

void clean_jobs();

/* FIX BUG 1: extern declarations for the job table so crash_recovery.c
 * (and any other TU) can access the live array defined in operators.c. */
extern Job  jobs[MAX_JOBS];
extern int  job_count;


// history.c
void init_history(void);
void save_history(void);
void add_to_history(const char* command);
void cleanup_history(void);
char** las_completion(const char* text, int start, int end);
void handle_sigint(int sig);
void handle_sigterm(int sig);
int  get_sigterm_received(void);

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

// Commandes internes pour la gestion des jobs
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
/* ── Las_shell: trading environment ── */
char** command_setmarket(char** args, char** env);
char** command_setbroker(char** args, char** env);
char** command_setaccount(char** args, char** env);
char** command_setcapital(char** args, char** env);
void   load_trading_env(char*** env_ptr);
void   save_trading_env(const char* key, const char* value);

/* ── Las_shell: work mode ── */
int    command_work(char** args);
void   set_work_mode(int on);
int    get_work_mode(void);

/* ── Las_shell: watch ── */
int  command_watch(char** args, char** env);
void set_watch_stop(int val);
int  get_watch_stop(void);

/* ── Las_shell: ~> backtest operator ── */
/* (parse_backtest_date and execute_backtest_op are static in operators.c) */
/* BACKTEST_MODE, BACKTEST_START, BACKTEST_END are injected into env       */

int command_assert(char** args, char** env);


/* ── Las_shell Phase 4.4: Crash Recovery & State Persistence ─────────────────
 * crash_recovery.c public API (abbreviated — full API in crash_recovery.h)
 */

/* ── Las_shell Phase 4.2: Broker API Bridge ──────────────────────────────────
 *
 * broker.c public command entry points.
 *
 * order buy|sell TICKER SIZE [market|limit PRICE] [--tif DAY|GTC|IOC|FOK]
 *   Route an order through the active adapter (paper ledger, Alpaca,
 *   IBKR, or generic REST).  Pre-trade risk limits from risk_config are
 *   applied before any network call.
 *
 * positions [--json] [--symbol TICKER]
 *   Print the current position table.  --json emits raw JSON for piping
 *   to jq.
 *
 * balance [--json]
 *   Print account equity summary.
 *
 * cancel ORDER_ID
 *   Cancel a pending order.  No-op in paper mode (all fills immediate).
 *
 * close_all
 *   Flatten every open position.  Used by the 'flatten' alias.
 *
 * reset_paper [--capital AMOUNT]
 *   Wipe the paper account ledger and start fresh.
 *
 * broker_status
 *   Print current broker configuration and connectivity.
 *
 * broker_get_pnl(env)
 *   Return total P&L and refresh ~/.las_shell_pnl for the prompt daemon.
 *
 * broker_paper_reset_state()
 *   Zero the in-process ledger (used by the test harness).
 * ─────────────────────────────────────────────────────────────────────── */

int get_last_exit_status(void);

/* ── Las_shell: $<() Streaming Substitution (Phase 3.1) ─────────────────────
 *
 * StreamSub represents a live named-pipe connection to a source command.
 * Lifecycle:
 *   stream_sub_open()      → fork source, open FIFO read-end
 *   stream_sub_read_line() → read one line per call (blocks until available)
 *   stream_sub_close()     → SIGTERM child, reap, unlink FIFO
 *   stream_sub_close_all() → emergency sweep (SIGINT / shell exit)
 */
#define STREAM_BUF_SIZE 4096

typedef struct {
    int    read_fd;                    /* FIFO read-end file descriptor         */
    pid_t  source_pid;                 /* PID of the child writing to the FIFO  */
    char   pipe_path[256];             /* Path to the mkfifo node               */
    /* Internal line-buffering state */
    char   buffer[STREAM_BUF_SIZE];    /* Read buffer to minimise syscalls       */
    int    buf_pos;                    /* Current read position in buffer        */
    int    buf_len;                    /* Valid bytes in buffer                  */
    int    eof;                        /* 1 once EOF has been seen               */
} StreamSub;

/* streaming_sub.c */
StreamSub* stream_sub_open(const char* cmd, char** env);
char*      stream_sub_read_line(StreamSub* ss);
void       stream_sub_close(StreamSub* ss);
void       stream_sub_close_all(void);

/* Exposed for the substitution engine */
char* find_streaming_substitution(const char* input, int* start_pos, int* end_pos);
char* process_line_with_streaming_subs(const char* input, char** env);
char* expand_streaming_substitution_oneshot(const char* cmd, char** env);
/* ── Las_shell Phase 4.1: Audit Mode & Compliance Log ───────────────────────
 *
 * audit.c public API.
 *
 * audit_init(path)
 *   Call once when --audit flag is detected.
 *   path = NULL → ~/.las_shell_audit
 *   Seeds chain from any existing log, writes SESSION_START marker.
 *
 * audit_is_enabled()
 *   Returns 1 if audit mode is active, 0 otherwise.
 *
 * audit_log_command(cmd, env)
 *   Execute cmd while capturing stdout for SHA-256 hashing.
 *   Appends a tamper-evident CSV record to the audit log.
 *   Returns the command's exit code.
 *   If audit is disabled, delegates directly to execute_command_line().
 *
 * audit_verify_log(path)
 *   Replay the chain hash from genesis.  Reports any break.
 *   path = NULL → use g_audit_path (or ~/.las_shell_audit if not set).
 *   Returns 0 = all OK, 1 = integrity violation detected.
 *
 * audit_show_log(tail_n)
 *   Pretty-print the last tail_n data records.
 *   tail_n <= 0 → print all.
 *
 * command_audit(args, env)
 *   Built-in dispatcher:
 *     audit verify [path]   → audit_verify_log()
 *     audit show   [N]      → audit_show_log(N)
 *     audit path            → print current log path
 *     audit status          → print enabled/disabled + record count
 *   Returns 0 on success, 1 on error.
 * ─────────────────────────────────────────────────────────────────────── */

void audit_init(const char *log_path);
int  audit_is_enabled(void);
int  audit_log_command(const char *cmd, char **env);
int  audit_verify_log(const char *log_path);
void audit_show_log(int tail_n);
int  command_audit(char **args, char **env);
const char *audit_get_path(void);
