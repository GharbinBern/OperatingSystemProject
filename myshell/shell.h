/*
 * shell.h
 *
 * Public interface for shell.c. This header exposes execute_command(), which
 * is the function used by server.c to run a shell command and get its output.
 *
 * shell.c acts as a bridge between the server and the parsing/execution
 * subsystem. It forks a child process, runs the command through parse_input()
 * and execute_pipeline(), captures all output through a pipe, and returns
 * the result as a heap-allocated string.
 */

#ifndef SHELL_H
#define SHELL_H

/*
 * execute_command - run a shell command string and return its output.
 *
 * Parameters:
 *   cmd - null-terminated command string (e.g. "ls -l", "cat file | grep x")
 *
 * Returns:
 *   A heap-allocated string with the combined stdout and stderr output of the
 *   command. On internal error (pipe or fork failure) it returns a string that
 *   begins with "Error:". The caller must free() the returned pointer.
 */
char *execute_command(const char *cmd);

#endif /* SHELL_H */
