#define _POSIX_C_SOURCE 200809L

#include "parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define PIPE_CHAR    '|'
#define INPUT_REDIR  '<'
#define OUTPUT_REDIR '>'
#define ERROR_REDIR  '2'

// forward declarations for internal helpers
static int   tokenize_command(const char *cmd_str, Command *cmd);
static char *next_token_quoted(const char **cursor, int *unmatched_quote);
static char *trim_whitespace(char *str);
static int   has_syntax_error(const char *input, char *error_msg);
static int   find_pipe_positions(const char *input, int *positions, int max_pipes);


// Scans input and records the byte index of every '|' that is outside quotes.
// This prevents splitting on pipes inside quoted strings like: grep "a|b" file.
// Returns the total number of unquoted pipes found.
static int find_pipe_positions(const char *input, int *positions, int max_pipes) {
    int count     = 0;
    int in_single = 0, in_double = 0;

    for (int i = 0; input[i]; i++) {
        if      (input[i] == '\'' && !in_double) in_single = !in_single;  // toggle single-quote mode
        else if (input[i] == '"'  && !in_single) in_double = !in_double;  // toggle double-quote mode
        else if (input[i] == '|'  && !in_single && !in_double) {
            if (count < max_pipes) positions[count] = i;  // record pipe position
            count++;
        }
    }
    return count;
}

// Prints a parser error to stderr in a consistent "Error: <message>" format.
void print_parse_error(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
}


// Converts the raw input string into a Pipeline of Commands.
// Steps: syntax check → find pipe positions → count segments →
//        allocate commands array → tokenize each segment.
// Sets command_count to -1 and prints an error if anything fails.
Pipeline parse_input(const char *input) {
    Pipeline pipeline  = {NULL, 0};  // start with an empty, valid pipeline
    char     error_msg[256];

    // treat null or empty input as zero commands (not an error)
    if (input == NULL || strlen(input) == 0) {
        pipeline.command_count = 0;
        return pipeline;
    }

    // reject obvious syntax errors before doing any allocation
    if (has_syntax_error(input, error_msg)) {
        print_parse_error(error_msg);
        pipeline.command_count = -1;
        return pipeline;
    }

    // locate all unquoted pipes so we know how many segments exist
    int pipe_positions[MAX_ARGS];
    int pipe_count    = find_pipe_positions(input, pipe_positions, MAX_ARGS);
    int segment_count = pipe_count + 1;  // N pipes produce N+1 segments

    // first pass: count non-empty segments to know how many Commands to allocate
    int cmd_count = 0;
    int seg_start = 0;
    for (int p = 0; p < segment_count; p++) {
        // segment ends at the next pipe position, or at end of string
        int   seg_end = (p < pipe_count) ? pipe_positions[p] : (int)strlen(input);
        int   seg_len = seg_end - seg_start;
        char *seg     = malloc(seg_len + 1);
        if (!seg) { perror("malloc"); pipeline.command_count = -1; return pipeline; }
        strncpy(seg, input + seg_start, seg_len);
        seg[seg_len] = '\0';
        if (strlen(trim_whitespace(seg)) > 0) cmd_count++;  // only count non-blank segments
        free(seg);
        seg_start = seg_end + 1;  // skip past the '|'
    }
    if (cmd_count == 0) {
        print_parse_error("Empty command");
        pipeline.command_count = -1;
        return pipeline;
    }

    // allocate the Command array now that we know the exact count
    pipeline.commands = malloc(cmd_count * sizeof(Command));
    if (!pipeline.commands) {
        perror("malloc");
        pipeline.command_count = -1;
        return pipeline;
    }

    // second pass: tokenize each non-blank segment into a Command struct
    int cmd_idx = 0;
    seg_start   = 0;
    for (int p = 0; p < segment_count; p++) {
        int   seg_end = (p < pipe_count) ? pipe_positions[p] : (int)strlen(input);
        int   seg_len = seg_end - seg_start;
        char *seg     = malloc(seg_len + 1);
        if (!seg) {
            perror("malloc");
            free_pipeline(&pipeline);
            pipeline.command_count = -1;
            return pipeline;
        }
        strncpy(seg, input + seg_start, seg_len);
        seg[seg_len]  = '\0';
        char *trimmed = trim_whitespace(seg);  // strip leading/trailing spaces

        if (strlen(trimmed) > 0) {
            Command *cmd = &pipeline.commands[cmd_idx];
            cmd->argc        = 0;     // no arguments yet
            cmd->input_file  = NULL;  // no redirections yet
            cmd->output_file = NULL;
            cmd->error_file  = NULL;

            // fill the Command with args and any redirections found in this segment
            if (tokenize_command(trimmed, cmd) == -1) {
                free(seg);
                free_pipeline(&pipeline);
                pipeline.command_count = -1;
                return pipeline;
            }
            // a segment that is only whitespace or operators would leave argc == 0
            if (cmd->argc == 0) {
                print_parse_error("Empty command between pipes");
                free(seg);
                free_pipeline(&pipeline);
                pipeline.command_count = -1;
                return pipeline;
            }
            cmd_idx++;
        }
        free(seg);
        seg_start = seg_end + 1;  // move past the '|'
    }

    pipeline.command_count = cmd_count;
    return pipeline;
}


// Breaks a single command string (no pipes) into tokens.
// Handles <, >, and 2> as redirection operators; everything else becomes an arg.
// Returns 0 on success, -1 on parse error.
static int tokenize_command(const char *cmd_str, Command *cmd) {
    const char *cursor         = cmd_str;
    int         unmatched_quote = 0;
    char       *token          = next_token_quoted(&cursor, &unmatched_quote);

    while (token != NULL && cmd->argc < MAX_ARGS - 1) {

        if (strcmp(token, "<") == 0) {
            // input redirection: next token is the source file
            free(token);
            token = next_token_quoted(&cursor, &unmatched_quote);
            if (unmatched_quote) { print_parse_error("Unmatched quote"); return -1; }
            if (token == NULL)   { print_parse_error("Missing filename after '<'"); return -1; }
            cmd->input_file = malloc(strlen(token) + 1);
            if (!cmd->input_file) { perror("malloc"); free(token); return -1; }
            strcpy(cmd->input_file, token);
            free(token);

        } else if (strcmp(token, ">") == 0) {
            // output redirection: next token is the destination file
            free(token);
            token = next_token_quoted(&cursor, &unmatched_quote);
            if (unmatched_quote) { print_parse_error("Unmatched quote"); return -1; }
            if (token == NULL)   { print_parse_error("Missing filename after '>'"); return -1; }
            // catch "2>" mistakenly parsed as ">" followed by "2>" token
            if (strlen(token) > 1 && token[0] == '2' && token[1] == '>') {
                print_parse_error("Invalid operator '2>' without space");
                free(token);
                return -1;
            }
            cmd->output_file = malloc(strlen(token) + 1);
            if (!cmd->output_file) { perror("malloc"); free(token); return -1; }
            strcpy(cmd->output_file, token);
            free(token);

        } else if (strcmp(token, "2>") == 0) {
            // stderr redirection: next token is the error output file
            free(token);
            token = next_token_quoted(&cursor, &unmatched_quote);
            if (unmatched_quote) { print_parse_error("Unmatched quote"); return -1; }
            if (token == NULL)   { print_parse_error("Missing filename after '2>'"); return -1; }
            cmd->error_file = malloc(strlen(token) + 1);
            if (!cmd->error_file) { perror("malloc"); free(token); return -1; }
            strcpy(cmd->error_file, token);
            free(token);

        } else {
            // ordinary argument: copy the token into the args array
            size_t len = strlen(token);
            char  *arg = malloc(len + 1);
            if (!arg) { perror("malloc"); free(token); return -1; }
            memcpy(arg, token, len + 1);  // memcpy preserves backslashes that strcpy would not
            cmd->args[cmd->argc++] = arg;
            free(token);
        }

        // advance to the next token and check for unmatched quotes
        token = next_token_quoted(&cursor, &unmatched_quote);
        if (unmatched_quote) { print_parse_error("Unmatched quote"); return -1; }
    }

    cmd->args[cmd->argc] = NULL;  // null-terminate the argv-style array
    free(token);
    return 0;
}


// Extracts the next token from *cursor, respecting single/double quotes and
// backslash escapes. Advances *cursor past the token.
// Returns a heap-allocated string, or NULL on EOF or unmatched-quote error.
static char *next_token_quoted(const char **cursor, int *unmatched_quote) {
    const char *ptr = *cursor;
    *unmatched_quote = 0;

    // skip any whitespace before the next token
    while (*ptr && isspace((unsigned char)*ptr)) ptr++;

    // nothing left in the input
    if (*ptr == '\0') { *cursor = ptr; return NULL; }

    // '<' and '>' are returned as standalone single-character tokens
    if (*ptr == '<' || *ptr == '>') {
        char *token = malloc(2);
        if (!token) { perror("malloc"); return NULL; }
        token[0] = *ptr; token[1] = '\0';
        *cursor = ptr + 1;
        return token;
    }

    // "2>" is returned as a standalone two-character token
    if (*ptr == '2' && *(ptr + 1) == '>') {
        char *token = malloc(3);
        if (!token) { perror("malloc"); return NULL; }
        token[0] = '2'; token[1] = '>'; token[2] = '\0';
        *cursor = ptr + 2;
        return token;
    }

    // allocate worst-case size (remaining input length)
    size_t max_len = strlen(ptr);
    char  *token   = malloc(max_len + 1);
    if (!token) { perror("malloc"); return NULL; }

    size_t out_idx  = 0;
    int    in_single = 0;  // inside single quotes
    int    in_double = 0;  // inside double quotes

    while (*ptr) {
        if (!in_single && !in_double) {
            if (isspace((unsigned char)*ptr)) break;          // whitespace ends the token
            if (*ptr == '<' || *ptr == '>') break;            // redirection operator starts a new token
            if (*ptr == '2' && *(ptr + 1) == '>') break;      // "2>" operator starts a new token

            if (*ptr == '\\') {
                char next = *(ptr + 1);
                if (next == '\'' || next == '"' || next == '\\') {
                    token[out_idx++] = next; ptr += 2;         // collapse escape: \\, \', \"
                } else if (next != '\0') {
                    // preserve \n, \t, etc. as two characters for echo -e to handle later
                    token[out_idx++] = *ptr; token[out_idx++] = next; ptr += 2;
                } else {
                    token[out_idx++] = *ptr; ptr++;            // trailing backslash: keep as-is
                }
                continue;
            }
            if (*ptr == '\'') { in_single = 1; ptr++; continue; }  // open single quote (not copied)
            if (*ptr == '"')  { in_double = 1; ptr++; continue; }  // open double quote (not copied)

        } else if (in_single) {
            // inside single quotes: everything is literal; only closing quote is special
            if (*ptr == '\'') { in_single = 0; ptr++; continue; }  // close single quote
            token[out_idx++] = *ptr++; continue;

        } else if (in_double) {
            // inside double quotes: only \" and \\ are collapsed; \' is kept as-is
            if (*ptr == '\\') {
                char next = *(ptr + 1);
                if (next == '"' || next == '\\') {
                    token[out_idx++] = next; ptr += 2;         // collapse \" and \\
                } else if (next != '\0') {
                    token[out_idx++] = *ptr; token[out_idx++] = next; ptr += 2;
                } else {
                    token[out_idx++] = *ptr; ptr++;
                }
                continue;
            }
            if (*ptr == '"') { in_double = 0; ptr++; continue; }   // close double quote
            token[out_idx++] = *ptr++; continue;
        }

        token[out_idx++] = *ptr++;  // ordinary character outside any quote
    }

    // a quote was opened but never closed — report the error
    if (in_single || in_double) {
        free(token);
        *unmatched_quote = 1;
        return NULL;
    }

    token[out_idx] = '\0';  // null-terminate the built token
    *cursor = ptr;           // advance the caller's cursor past this token
    return token;
}


// Strips leading and trailing whitespace from str in place.
// Returns a pointer to the first non-whitespace character.
static char *trim_whitespace(char *str) {
    while (*str && isspace((unsigned char)*str)) str++;  // skip leading spaces
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;  // find last non-space
    *(end + 1) = '\0';  // chop off trailing spaces
    return str;
}


// Scans input for structural syntax errors.
// Currently catches: leading pipe, trailing pipe, and double pipe ("||").
// Writes a descriptive message into error_msg and returns 1 if an error is found.
static int has_syntax_error(const char *input, char *error_msg) {
    const char *ptr = input;
    while (*ptr) {
        if (*ptr == '|') {
            // check whether everything before this '|' is whitespace (leading pipe)
            const char *before = input;
            while (before < ptr && isspace((unsigned char)*before)) before++;
            if (before == ptr) {
                strcpy(error_msg, "Pipe cannot be at the beginning");
                return 1;
            }

            // look ahead past whitespace to find double pipe or trailing pipe
            const char *temp = ptr + 1;
            while (*temp && isspace((unsigned char)*temp)) temp++;
            if (*temp == '|')  { strcpy(error_msg, "Invalid pipe operator"); return 1; }
            if (*temp == '\0') { strcpy(error_msg, "Pipe cannot be at the end"); return 1; }
        }
        ptr++;
    }
    return 0;
}


// Releases all memory owned by pipeline: each Command's args and redirection
// file strings, the commands array itself, and resets the struct fields.
void free_pipeline(Pipeline *pipeline) {
    if (pipeline == NULL || pipeline->commands == NULL) return;

    for (int i = 0; i < pipeline->command_count; i++) {
        Command *cmd = &pipeline->commands[i];

        // free each individually heap-allocated argument string
        for (int j = 0; j < cmd->argc; j++) free(cmd->args[j]);

        // free redirection filenames if they were set
        if (cmd->input_file)  free(cmd->input_file);
        if (cmd->output_file) free(cmd->output_file);
        if (cmd->error_file)  free(cmd->error_file);
    }

    free(pipeline->commands);      // free the Command array itself
    pipeline->commands     = NULL;
    pipeline->command_count = 0;
}
