#include "execute.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Opens the redirection files specified in cmd and calls dup2() to wire them
// to STDIN, STDOUT, or STDERR. Called inside the child process before execvp().
// Returns 0 on success, -1 if any open() or dup2() fails.
static int apply_redirections(const Command *cmd) {
    int fd;

    // input redirection: open file for reading and connect to stdin
    if (cmd->input_file != NULL) {
        fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) { perror(cmd->input_file); return -1; }
        if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
        close(fd);  // fd no longer needed; stdin now points at the file
    }

    // output redirection: create/truncate file and connect to stdout
    if (cmd->output_file != NULL) {
        fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror(cmd->output_file); return -1; }
        if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
        close(fd);
    }

    // error redirection: create/truncate file and connect to stderr
    if (cmd->error_file != NULL) {
        fd = open(cmd->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror(cmd->error_file); return -1; }
        if (dup2(fd, STDERR_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
        close(fd);
    }

    return 0;
}

// Executes every command in the pipeline.
// For each command: creates a pipe (except for the last), forks a child,
// wires up the inter-process pipe fds, applies redirections, then exec's.
// The parent waits for all children to finish before returning.
// Returns 0 on success, -1 on any internal error.
int execute_pipeline(const Pipeline *pipeline) {
    int    prev_read_fd = -1;      // read end of the previous command's pipe
    int    pipe_fds[2]  = {-1, -1}; // pipe connecting current command to next
    pid_t *child_pids;             // array of all forked PIDs for waitpid

    // guard against null or empty pipeline
    if (pipeline == NULL || pipeline->commands == NULL || pipeline->command_count <= 0)
        return -1;

    // allocate PID array so parent can wait for every child
    child_pids = malloc((size_t)pipeline->command_count * sizeof(pid_t));
    if (child_pids == NULL) { perror("malloc"); return -1; }

    for (int i = 0; i < pipeline->command_count; i++) {
        const Command *cmd     = &pipeline->commands[i];
        int            is_last = (i == pipeline->command_count - 1);

        // create a new pipe for every command except the last
        if (!is_last) {
            if (pipe(pipe_fds) < 0) {
                perror("pipe");
                if (prev_read_fd != -1) close(prev_read_fd);
                for (int j = 0; j < i; j++) waitpid(child_pids[j], NULL, 0);
                free(child_pids);
                return -1;
            }
        }

        // fork a child for this command
        child_pids[i] = fork();
        if (child_pids[i] < 0) {
            perror("fork");
            if (!is_last) { close(pipe_fds[0]); close(pipe_fds[1]); }
            if (prev_read_fd != -1) close(prev_read_fd);
            for (int j = 0; j < i; j++) waitpid(child_pids[j], NULL, 0);
            free(child_pids);
            return -1;
        }

        if (child_pids[i] == 0) {
            // --- child process ---

            // connect the previous pipe's read end to stdin
            if (prev_read_fd != -1) {
                if (dup2(prev_read_fd, STDIN_FILENO) < 0) { perror("dup2"); _exit(EXIT_FAILURE); }
            }

            // connect stdout to the write end of the current pipe
            if (!is_last) {
                if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) { perror("dup2"); _exit(EXIT_FAILURE); }
            }

            // close inherited fds the child no longer needs
            if (prev_read_fd != -1) close(prev_read_fd);
            if (!is_last) { close(pipe_fds[0]); close(pipe_fds[1]); }

            // apply <, >, and 2> redirections specified in the command
            if (apply_redirections(cmd) < 0) {
                fprintf(stderr, "Error: Redirection failed for command: %s\n",
                        cmd->args[0] ? cmd->args[0] : "(null)");
                _exit(EXIT_FAILURE);
            }

            // handle "echo" as a shell builtin before trying execvp
            if (cmd->argc > 0 && strcmp(cmd->args[0], "echo") == 0) {
                int interpret_escapes = 0;  // set when -e flag is present
                int start = 1;              // index of the first argument to print
                if (cmd->argc > 1 && strcmp(cmd->args[1], "-e") == 0) {
                    interpret_escapes = 1;
                    start = 2;  // skip the -e flag itself
                }
                for (int j = start; j < cmd->argc; ++j) {
                    if (j > start) printf(" ");  // space between arguments
                    if (interpret_escapes) {
                        // walk each character and expand \n, \t, \\ sequences
                        for (char *p = cmd->args[j]; *p; ++p) {
                            if (*p == '\\' && *(p + 1)) {
                                ++p;
                                if      (*p == 'n')  putchar('\n');
                                else if (*p == 't')  putchar('\t');
                                else if (*p == '\\') putchar('\\');
                                else                 putchar(*p);
                            } else {
                                putchar(*p);
                            }
                        }
                    } else {
                        printf("%s", cmd->args[j]);  // print argument as-is
                    }
                }
                printf("\n");   // echo always ends with a newline
                fflush(stdout);
                _exit(0);       // builtin handled; exit child without exec
            }

            // replace the child image with the requested program
            execvp(cmd->args[0], cmd->args);
            // execvp only returns on failure
            fprintf(stderr, "Error: Command not found: %s\n",
                    cmd->args[0] ? cmd->args[0] : "(null)");
            _exit(127);
        }

        // --- parent process ---

        // done with previous pipe's read end; child already duplicated it
        if (prev_read_fd != -1) close(prev_read_fd);

        if (!is_last) {
            close(pipe_fds[1]);           // parent doesn't write to this pipe
            prev_read_fd = pipe_fds[0];   // save read end for the next child's stdin
        } else {
            prev_read_fd = -1;
        }
    }

    // safety close: should be -1 here, but guard anyway
    if (prev_read_fd != -1) close(prev_read_fd);

    // wait for every child in the pipeline to finish
    for (int i = 0; i < pipeline->command_count; i++) {
        int   status;
        pid_t waited_pid;
        // retry on EINTR (signal interrupted the wait)
        do {
            waited_pid = waitpid(child_pids[i], &status, 0);
        } while (waited_pid < 0 && errno == EINTR);

        if (waited_pid < 0) {
            perror("waitpid");
            free(child_pids);
            return -1;
        }
    }

    free(child_pids);
    return 0;
}
