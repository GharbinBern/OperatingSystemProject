#ifndef EXECUTE_H
#define EXECUTE_H

#include "parse.h"

/*
   Execution module interface.
 
   This module receives a validated Pipeline from the parser and performs
   process creation/execution, redirection, and pipe wiring.
 */

/*
   Executes a parsed pipeline using fork/exec, pipes, and redirection.
 
   Parameter:
    pipeline - Pointer to a parsed pipeline structure.
               Expected to be validated by parse_input().
 
   Returns:
    0  on success (all process creation/wait operations completed)
   -1  on internal execution errors (pipe/fork/wait/open/dup2 failures)
 */
int execute_pipeline(const Pipeline *pipeline);

#endif // EXECUTE_H
