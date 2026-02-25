#include "execute.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/*
  Applies file redirections for a single command.
  Returns 0 on success, -1 on failure.
 */
static int apply_redirections(const Command *cmd) {
    int fd;

    // Handle input redirection: command < file
    if (cmd->input_file != NULL) {
        fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) {
            perror(cmd->input_file);
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }

    // Handle output redirection: command > file 
    if (cmd->output_file != NULL) {
        fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror(cmd->output_file);
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }

    // Handle error redirection: command 2> file 
    if (cmd->error_file != NULL) {
        fd = open(cmd->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror(cmd->error_file);
            return -1;
        }
        if (dup2(fd, STDERR_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }

    return 0;
}

/*
   Executes all commands in a parsed pipeline.
   Supports single commands, redirections, and multiple pipes.
 */
int execute_pipeline(const Pipeline *pipeline) {
    int prev_read_fd = -1;
    int pipe_fds[2] = {-1, -1};
    pid_t *child_pids;

    // checks before execution. 
    if (pipeline == NULL || pipeline->commands == NULL || pipeline->command_count <= 0) {
        return -1;
    }

    // Track all child PIDs so parent can wait for each one. 
    child_pids = malloc((size_t)pipeline->command_count * sizeof(pid_t));
    if (child_pids == NULL) {
        perror("malloc");
        return -1;
    }

    // Iterate through each command in the pipeline.
    for (int i = 0; i < pipeline->command_count; i++) {
        const Command *cmd = &pipeline->commands[i];
        int is_last = (i == pipeline->command_count - 1);

        // Create a pipe for every command except the last one. 
        if (!is_last) {
            if (pipe(pipe_fds) < 0) {
                perror("pipe");
                if (prev_read_fd != -1) {
                    close(prev_read_fd);
                }
                for (int j = 0; j < i; j++) {
                    waitpid(child_pids[j], NULL, 0);
                }
                free(child_pids);
                return -1;
            }
        }

        // Fork child to execute the current command. 
        child_pids[i] = fork();
        if (child_pids[i] < 0) {
            perror("fork");

            if (!is_last) {
                close(pipe_fds[0]);
                close(pipe_fds[1]);
            }
            if (prev_read_fd != -1) {
                close(prev_read_fd);
            }

            for (int j = 0; j < i; j++) {
                waitpid(child_pids[j], NULL, 0);
            }
            free(child_pids);
            return -1;
        }

        if (child_pids[i] == 0) {
            // If there is input from a previous pipe, connect it to stdin.
            if (prev_read_fd != -1) {
                if (dup2(prev_read_fd, STDIN_FILENO) < 0) {
                    perror("dup2");
                    _exit(EXIT_FAILURE);
                }
            }

            // If not last command, connect stdout to current pipe's write end. 
            if (!is_last) {
                if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
                    perror("dup2");
                    _exit(EXIT_FAILURE);
                }
            }

            // Close inherited fds no longer needed in child.
            if (prev_read_fd != -1) {
                close(prev_read_fd);
            }
            if (!is_last) {
                close(pipe_fds[0]);
                close(pipe_fds[1]);
            }

            // Apply <, >, and 2> redirections for this command.
            if (apply_redirections(cmd) < 0) {
                _exit(EXIT_FAILURE);
            }

            // Replace child process image with the target command.
            execvp(cmd->args[0], cmd->args);
            perror(cmd->args[0]);
            _exit(127);
        }

        // Parent: close previous read end after child has duplicated it. 
        if (prev_read_fd != -1) {
            close(prev_read_fd);
        }

        // Parent: keep read end of current pipe for next command. 
        if (!is_last) {
            close(pipe_fds[1]);
            prev_read_fd = pipe_fds[0];
        } else {
            prev_read_fd = -1;
        }
    }

    if (prev_read_fd != -1) {
        close(prev_read_fd);
    }

    // Wait for every child in the pipeline to finish.
    for (int i = 0; i < pipeline->command_count; i++) {
        int status;
        pid_t waited_pid;

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
