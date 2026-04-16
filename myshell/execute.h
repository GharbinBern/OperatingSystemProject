/*
 * execute.h
 *
 * Public interface for execute.c. This module takes a fully parsed Pipeline
 * and is responsible for running the commands it contains. It handles:
 *
 *   - Creating a child process for each command in the pipeline
 *   - Wiring stdout of each command to stdin of the next using pipes
 *   - Applying input, output, and error redirections for each command
 *   - Waiting for all child processes to finish before returning
 */

#ifndef EXECUTE_H
#define EXECUTE_H

#include "parse.h"

/*
 * execute_pipeline - fork and exec every command in a parsed pipeline.
 *
 * For a single command, one child is forked and execvp() is called.
 * For N commands separated by '|', N children are created and connected
 * through pipes so each command's output feeds the next command's input.
 * File redirections specified per-command are applied inside each child
 * process before the exec call.
 *
 * Parameters:
 *   pipeline - pointer to a Pipeline struct populated by parse_input()
 *
 * Returns:
 *    0 if all processes were created and waited on successfully
 *   -1 if an internal error occurred (pipe, fork, or malloc failure)
 */
int execute_pipeline(const Pipeline *pipeline);

#endif /* EXECUTE_H */
