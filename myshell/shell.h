// shell.h
// Declares execute_command(), which runs a shell command string and returns
// its output as a heap-allocated string. The caller must free the string.

#ifndef SHELL_H
#define SHELL_H

// Forks a child process, executes cmd through the parser and executor,
// captures all stdout and stderr output via a pipe, and returns it.
// Returns a string starting with "Error:" if the command fails or is not found.
char *execute_command(const char *cmd);

#endif /* SHELL_H */
