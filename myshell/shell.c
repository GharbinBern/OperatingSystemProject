#include "shell.h"
#include "parse.h"
#include "execute.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define CMD_OUTPUT_SIZE 8192

// Executes a shell command string by parsing and running it in a child process.
// Captures all output (stdout and stderr) and returns it as a heap-allocated string.
// The caller must free this string. If an error occurs, returns an error message string.
char* execute_command(const char* cmd) {
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
        // --- CHILD process ---
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        Pipeline pipeline = parse_input(cmd);
        if (pipeline.command_count == -1) {
            // parse_input already printed the specific error message
            _exit(1);
        }
        if (pipeline.command_count == 0) {
            // Empty input: exit quietly
            _exit(0);
        }

        execute_pipeline(&pipeline);
        free_pipeline(&pipeline);
        _exit(0);
    }

    // --- PARENT process ---
    close(pipefd[1]);

    char* output = malloc(CMD_OUTPUT_SIZE);
    if (!output) {
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return NULL;
    }

    size_t total = 0;
    ssize_t n;
    // Read all bytes from the pipe until the child closes it or buffer is full
    while ((n = read(pipefd[0], output + total, CMD_OUTPUT_SIZE - 1 - total)) > 0) {
        total += (size_t)n;
        if (total >= CMD_OUTPUT_SIZE - 1) break;
    }
    output[total] = '\0'; // Null-terminate the output string
    close(pipefd[0]);

    int status;
    // Wait for child to finish
    waitpid(pid, &status, 0);

    // If the child failed and produced no output, synthesize an error message
    if (total == 0 && WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        free(output);
        return strdup("Error: Command not found\n");
    }

    return output;
}
