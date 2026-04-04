#ifndef PARSE_H
#define PARSE_H

#include <stddef.h>

#define MAX_ARGS 64
#define MAX_COMMANDS 32
#define MAX_FILENAME 256


/**
 * Represents a single command in the pipeline.
 *
 * Fields:
 *   args        - NULL-terminated array of argument strings (argv-style)
 *   argc        - Number of arguments
 *   input_file  - Input redirection file (if any, for '<')
 *   output_file - Output redirection file (if any, for '>')
 *   error_file  - Error redirection file (if any, for '2>')
 */
typedef struct {
    char *args[MAX_ARGS];
    int argc;
    char *input_file;
    char *output_file;
    char *error_file;
} Command;


/**
 * Represents a pipeline of commands connected by pipes.
 *
 * Fields:
 *   commands      - Array of Command structs (one per command in pipeline)
 *   command_count - Number of commands in the pipeline
 */
typedef struct {
    Command *commands;
    int command_count;
} Pipeline;


Pipeline parse_input(const char *input);
void free_pipeline(Pipeline *pipeline);
void print_parse_error(const char *message);

#endif // PARSE_H
