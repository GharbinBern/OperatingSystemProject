#ifndef PARSE_H
#define PARSE_H

#include <stddef.h>

#define MAX_ARGS     64
#define MAX_COMMANDS 32
#define MAX_FILENAME 256

// One command in the pipeline: its arguments and any redirection files.
typedef struct {
    char *args[MAX_ARGS];
    int   argc;
    char *input_file;
    char *output_file;
    char *error_file;
} Command;

// A pipeline of one or more commands connected by '|'.
typedef struct {
    Command *commands;
    int      command_count;
} Pipeline;

Pipeline parse_input(const char *input);
void     free_pipeline(Pipeline *pipeline);
void     print_parse_error(const char *message);

#endif // PARSE_H
