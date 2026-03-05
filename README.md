# LAS Shell

A comprehensive UNIX shell implementation in C, supporting built-in commands, external program execution, piping, redirection, job control, and scripting capabilities.

## Overview

LAS Shell is a fully functional command-line interpreter designed and implemented from scratch in C. It provides a robust environment for command execution with features comparable to traditional UNIX shells.

## Features

### Core Functionality
- **Built-in Commands**: cd, pwd, echo, env, setenv, unsetenv, which, exit
- **External Command Execution**: Full support for system commands
- **Command Parsing**: Advanced tokenization with quote handling
- **Signal Handling**: Proper SIGINT (Ctrl+C) management

### Advanced Features
- **Pipes**: Support for command piping (|) with multiple stages
- **Redirections**: Input (<), output (>), and append (>>) redirections
- **Control Operators**: Command sequencing (;), conditional execution (&&, ||)
- **Background Processes**: Asynchronous execution with job control (&)
- **Job Management**: jobs, fg, bg commands for process control

### User Experience
- **Command History**: Navigation through previous commands with arrow keys
- **Aliases**: Persistent alias creation and management
- **Command Substitution**: $(command) syntax with nested substitution support
- **Script Execution**: Shebang support for script files
- **Custom Prompt**: Configurable prompt with exit status indication

## Technical Architecture

### Component Structure
- **Command Parser**: Tokenizes input with quote and whitespace handling
- **Execution Engine**: Manages process creation and pipeline construction
- **Memory Management**: Systematic allocation and deallocation
- **Signal Handlers**: Graceful interruption handling
- **State Management**: Environment variables, aliases, job control

### Key Modules
- `main.c`: Core shell loop and command dispatching
- `commands.c`: Built-in command implementations
- `parser.c`: Input parsing and tokenization
- `execution.c`: Process execution and pipeline management
- `job_control.c`: Background process and job management
- `alias.c`: Alias storage and expansion
- `history.c`: Command history with readline integration

## Requirements

- GCC compiler
- GNU Readline library
- UNIX-like operating system (Linux, macOS)

## Installation

```bash
# Clone the repository
git clone https://github.com/yourusername/las-shell.git
cd las-shell

# Compile the source
make

# Install system-wide (optional)
sudo make install