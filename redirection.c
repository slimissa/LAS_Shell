#include "my_own_shell.h"

int execute_with_redirect(char** args, char** env, char* output_file, int append_mode) {
    pid_t pid = fork();
    
    if (pid == 0) {
        // Processus enfant
        
        // Redirection de sortie (déjà existante)
        if (output_file != NULL) {
            int fd;
            if (append_mode) {
                fd = open(output_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
            } else {
                fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            }
            
            if (fd == -1) {
                perror("open");
                exit(1);
            }
            
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        
        execvp(args[0], args);
        fprintf(stderr, "Command not found: %s\n", args[0]);
        exit(1);
    }
    else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return status;
    }
    else {
        perror("fork");
        return -1;
    }
}

// NOUVELLE FONCTION : Redirection d'entrée
int execute_with_input_redirect(char** args, char** env, char* input_file) {
    pid_t pid = fork();
    
    if (pid == 0) {
        // Processus enfant
        
        // Redirection d'entrée depuis le fichier
        if (input_file != NULL) {
            int fd = open(input_file, O_RDONLY);
            if (fd == -1) {
                perror("open");
                exit(1);
            }
            
            // Rediriger stdin vers le fichier
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        
        execvp(args[0], args);
        fprintf(stderr, "Command not found: %s\n", args[0]);
        exit(1);
    }
    else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return status;
    }
    else {
        perror("fork");
        return -1;
    }
}

// Fonction combinée pour gérer les deux redirections
int execute_with_both_redirect(char** args, char** env, 
                               char* input_file, 
                               char* output_file, 
                               int append_mode) {
    pid_t pid = fork();
    
    if (pid == 0) {
        // Processus enfant
        
        // Redirection d'entrée (si spécifiée)
        if (input_file != NULL) {
            int fd_in = open(input_file, O_RDONLY);
            if (fd_in == -1) {
                perror("open input");
                exit(1);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }
        
        // Redirection de sortie (si spécifiée)
        if (output_file != NULL) {
            int fd_out;
            if (append_mode) {
                fd_out = open(output_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
            } else {
                fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            }
            if (fd_out == -1) {
                perror("open output");
                exit(1);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }
        
        execvp(args[0], args);
        fprintf(stderr, "Command not found: %s\n", args[0]);
        exit(1);
    }
    else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return status;
    }
    else {
        perror("fork");
        return -1;
    }
}