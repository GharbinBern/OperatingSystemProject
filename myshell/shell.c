/*
 * shell.c — Phase 2 Command Execution Bridge
 *
 * Provides execute_command(), which runs a command string through the
 * Phase 1 parser and executor and returns the combined stdout/stderr
 * output as a heap-allocated string (caller must free).
 *
 * Because execute_pipeline() writes directly to file descriptors, we
 * wrap it in a child process that redirects both stdout and stderr into
 * a pipe so the parent can collect the bytes into a buffer.
 */

#include "shell.h"
#include "parse.h"
#include "execute.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define CMD_OUTPUT_SIZE 8192

char *execute_command(const char *cmd) {
    int pipefd[2];

    if (pipe(pipefd) < 0) {
        return strdup("Error: pipe failed\n");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return strdup("Error: fork failed\n");
    }

    if (pid == 0) {
        /*
         * Child process: redirect both stdout and stderr into the pipe
         * write end, then parse and execute the command normally.
         * All output (including error messages from execvp) is captured.
         */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        Pipeline pipeline = parse_input(cmd);
        if (pipeline.command_count == -1) {
            fprintf(stderr, "Error: parse error\n");
            _exit(1);
        }
        if (pipeline.command_count == 0) {
            _exit(0);
        }

        execute_pipeline(&pipeline);
        free_pipeline(&pipeline);
        _exit(0);
    }

    /*
     * Parent process: close the write end and read all output the child
     * produced into a fixed-size buffer.
     */
    close(pipefd[1]);

    char *output = malloc(CMD_OUTPUT_SIZE);
    if (!output) {
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return NULL;
    }

    size_t total = 0;
    ssize_t n;
    while ((n = read(pipefd[0], output + total, CMD_OUTPUT_SIZE - 1 - total)) > 0) {
        total += (size_t)n;
        if (total >= CMD_OUTPUT_SIZE - 1) break;
    }
    output[total] = '\0';
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    /*
     * If the child exited with a non-zero status and produced no output,
     * synthesise an error string so the server can detect and log it.
     */
    if (total == 0 && WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        free(output);
        return strdup("Error: Command not found\n");
    }

    return output;
}
