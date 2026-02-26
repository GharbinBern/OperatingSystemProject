#ifndef PARSE_H
#define PARSE_H

#include <stddef.h>

#define MAX_ARGS 64
#define MAX_COMMANDS 32
#define MAX_FILENAME 256

/**
 * Represents a single command in the pipeline.
 * contains the command arguments and any redirection files.
 */
typedef struct {
    char *args[MAX_ARGS];           // NULL-terminated array of arguments
    int argc;                        // Number of arguments
    char *input_file;               // Input redirection file (< filename)
    char *output_file;              // Output redirection file (> filename)
    char *error_file;               // Error redirection file (2> filename)
} Command;

/**
 * Represents a pipeline of commands connected by pipes.
 * multiple commands can be chained with | operators.
 */
typedef struct {
    Command *commands;              // Array of commands
    int command_count;              // Number of commands in the pipeline
} Pipeline;

/**
 * Parses a user input string into a Pipeline structure.
 * validates syntax and detects errors during parsing.
 * Returns a Pipeline struct with parsed commands.
 * if there's an error, returns a Pipeline with command_count = -1 and an error message is printed.
 */
Pipeline parse_input(const char *input);

//Frees all memory allocated for a Pipeline and its contained commands.
void free_pipeline(Pipeline *pipeline);

//Prints detailed error messages for common parsing errors.
void print_parse_error(const char *message);

#endif // PARSE_H
