
/*
 * shell.c
 *
 * This module provides the execute_command() function, which acts as a bridge between
 * the shell's parser/executor and the network server. It takes a command string,
 * parses it, executes it, and returns all output (stdout and stderr) as a single
 * heap-allocated string. This is essential for the server to capture and forward
 * all command output to the client, including error messages.
 *
 * The core logic is:
 *   - Fork a child process to run the command pipeline.
 *   - In the child, redirect both stdout and stderr to a pipe, so all output is captured.
 *   - Parse and execute the command pipeline in the child.
 *   - In the parent, read all output from the pipe into a buffer.
 *   - Return the buffer as a string to the caller (server).
 *
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

/*
 * Executes a shell command string by parsing and running it in a child process.
 * Captures all output (stdout and stderr) and returns it as a heap-allocated string.
 *
 * Parameters:
 *   cmd - The command line string to execute (may contain pipes and redirections).
 *
 * Returns:
 *   Pointer to a heap-allocated string containing all output (stdout and stderr).
 *   The caller must free this string. If an error occurs, returns an error message string.
 */
char *execute_command(const char *cmd) {
    int pipefd[2]; // pipefd[0]: read end, pipefd[1]: write end

    // Create a pipe for capturing both stdout and stderr from the child
    if (pipe(pipefd) < 0) {
        return strdup("Error: pipe failed\n");
    }

    pid_t pid = fork();
    if (pid < 0) {
        // Fork failed; clean up and return error
        close(pipefd[0]);
        close(pipefd[1]);
        return strdup("Error: fork failed\n");
    }

    if (pid == 0) {
        // --- CHILD process ---
        // Redirect stdout and stderr to the pipe's write end
        close(pipefd[0]); // Close unused read end
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // Parse the command string into a pipeline structure
        Pipeline pipeline = parse_input(cmd);
        if (pipeline.command_count == -1) {
            // Parsing error: print error to stderr (captured by pipe)
            fprintf(stderr, "Error: parse error\n");
            _exit(1);
        }
        if (pipeline.command_count == 0) {
            // Empty input: exit quietly
            _exit(0);
        }

        // Execute the parsed pipeline (handles pipes, redirections, etc.)
        execute_pipeline(&pipeline);
        free_pipeline(&pipeline);
        _exit(0);
    }

    // --- PARENT process ---
    // Close the write end and read all output from the child into a buffer
    close(pipefd[1]);

    char *output = malloc(CMD_OUTPUT_SIZE);
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
    waitpid(pid, &status, 0); // Wait for child to finish

    // If the child failed and produced no output, synthesize an error message
    if (total == 0 && WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        free(output);
        return strdup("Error: Command not found\n");
    }

    return output;
}
