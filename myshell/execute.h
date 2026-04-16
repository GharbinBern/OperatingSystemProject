// execute.h
// Declares execute_pipeline(), which forks and execs every command in a
// parsed Pipeline, wires pipes between them, and applies redirections.

#ifndef EXECUTE_H
#define EXECUTE_H

#include "parse.h"

// Executes all commands in the pipeline. For a single command, one child
// is forked. For N commands, N children are created and connected with pipes.
// Returns 0 on success, -1 if a pipe, fork, or malloc call fails.
int execute_pipeline(const Pipeline *pipeline);

#endif /* EXECUTE_H */
