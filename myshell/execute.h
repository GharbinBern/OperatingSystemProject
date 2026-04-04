#ifndef EXECUTE_H
#define EXECUTE_H

#include "parse.h"

/*
 * execute.h
 *
 * This module receives a validated Pipeline from the parser and performs:
 *   - Process creation and execution (fork/exec)
 *   - Pipe wiring between commands
 *   - Input/output/error redirection
 *
 * The main entry point is execute_pipeline(), which executes all commands in the pipeline.
 */


int execute_pipeline(const Pipeline *pipeline);

#endif // EXECUTE_H
