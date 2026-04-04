
/*
 * parse.c 
 *
 * This module is responsible for parsing a shell command line string into a structured Pipeline,
 * which consists of one or more Command objects. It handles splitting by pipes, tokenizing arguments,
 * and extracting input/output/error redirections. It also performs syntax validation and error reporting.
 *
 * The parser supports:
 *   - Multiple commands separated by '|'
 *   - Arguments with or without quotes
 *   - Input (<), output (>), and error (2>) redirection
 *   - Syntax error detection (unmatched quotes, misplaced pipes, missing filenames, etc.)
 *
 * All memory allocated for the pipeline and commands must be freed by calling free_pipeline().
 */

#define _POSIX_C_SOURCE 200809L

#include "parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> // for character helpers (isspace)

// Constants for pipe and redirection operators
#define PIPE_CHAR '|'
#define INPUT_REDIR '<'
#define OUTPUT_REDIR '>'
#define ERROR_REDIR '2'

// Forward declarations for internal helper functions
static int tokenize_command(const char *cmd_str, Command *cmd);
static char *next_token_quoted(const char **cursor, int *unmatched_quote);
static char *trim_whitespace(char *str);
static int has_syntax_error(const char *input, char *error_msg);
static int find_pipe_positions(const char *input, int *positions, int max_pipes);

/*
 * Scans input and records the index of every '|' that is outside quotes.
 * This prevents splitting on pipes inside quoted strings like grep "a|b" file.
 * Returns the number of pipes found.
 */
static int find_pipe_positions(const char *input, int *positions, int max_pipes) {
    int count = 0;
    int in_single = 0, in_double = 0;
    for (int i = 0; input[i]; i++) {
        if (input[i] == '\'' && !in_double) in_single = !in_single;       // toggle single-quote mode
        else if (input[i] == '"' && !in_single) in_double = !in_double;   // toggle double-quote mode
        else if (input[i] == '|' && !in_single && !in_double) {            // real pipe outside quotes
            if (count < max_pipes) positions[count] = i;
            count++;
        }
    }
    return count;
}

// Print a parser error message to stderr in a consistent format
void print_parse_error(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
}


/*
 * Parses a shell input string into a Pipeline structure.
 *
 * This function splits the input by pipes, tokenizes each command, extracts arguments and redirections,
 * and validates syntax. If parsing fails, command_count is set to -1 and an error is printed.
 *
 * Parameters:
 *   input - The command line string to parse.
 *
 * Returns:
 *   Pipeline struct with parsed commands. If parsing fails, command_count = -1.
 */
Pipeline parse_input(const char *input) {
    Pipeline pipeline = {NULL, 0};
    char error_msg[256];

    // Check for empty input (treat as no commands)
    if (input == NULL || strlen(input) == 0) {
        pipeline.command_count = 0;
        return pipeline;
    }

    // Check for syntax errors before parsing
    if (has_syntax_error(input, error_msg)) {
        print_parse_error(error_msg);
        pipeline.command_count = -1;
        return pipeline;
    }

    // Find pipe positions using a quote-aware scanner so pipes inside
    // quoted strings (e.g. grep "a|b" file) are not treated as separators.
    int pipe_positions[MAX_ARGS];
    int pipe_count = find_pipe_positions(input, pipe_positions, MAX_ARGS);
    int segment_count = pipe_count + 1;

    // First pass: count non-empty command segments
    int cmd_count = 0;
    int seg_start = 0;
    for (int p = 0; p < segment_count; p++) {
        int seg_end = (p < pipe_count) ? pipe_positions[p] : (int)strlen(input);
        int seg_len = seg_end - seg_start;
        char *seg = malloc(seg_len + 1);
        if (!seg) { perror("malloc"); pipeline.command_count = -1; return pipeline; }
        strncpy(seg, input + seg_start, seg_len);
        seg[seg_len] = '\0';
        if (strlen(trim_whitespace(seg)) > 0) cmd_count++;
        free(seg);
        seg_start = seg_end + 1;
    }
    if (cmd_count == 0) {
        print_parse_error("Empty command");
        pipeline.command_count = -1;
        return pipeline;
    }

    // Allocate space for all commands in the pipeline
    pipeline.commands = malloc(cmd_count * sizeof(Command));
    if (!pipeline.commands) {
        perror("malloc");
        pipeline.command_count = -1;
        return pipeline;
    }

    // Second pass: tokenize and parse each non-empty segment into a Command
    int cmd_idx = 0;
    seg_start = 0;
    for (int p = 0; p < segment_count; p++) {
        int seg_end = (p < pipe_count) ? pipe_positions[p] : (int)strlen(input);
        int seg_len = seg_end - seg_start;
        char *seg = malloc(seg_len + 1);
        if (!seg) {
            perror("malloc");
            free_pipeline(&pipeline);
            pipeline.command_count = -1;
            return pipeline;
        }
        strncpy(seg, input + seg_start, seg_len);
        seg[seg_len] = '\0';
        char *trimmed = trim_whitespace(seg);

        if (strlen(trimmed) > 0) {
            Command *cmd = &pipeline.commands[cmd_idx];
            cmd->argc = 0;
            cmd->input_file = NULL;
            cmd->output_file = NULL;
            cmd->error_file = NULL;

            if (tokenize_command(trimmed, cmd) == -1) {
                free(seg);
                free_pipeline(&pipeline);
                pipeline.command_count = -1;
                return pipeline;
            }
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
        seg_start = seg_end + 1;
    }

    pipeline.command_count = cmd_count;
    return pipeline;
}

/*
 * Tokenizes a single command string, extracting arguments and redirection files.
 * Handles quoted arguments and validates redirection syntax.
 *
 * Parameters:
 *   cmd_str - The command string to tokenize (no pipes)
 *   cmd     - Pointer to Command struct to fill
 *
 * Returns:
 *   0 on success, -1 on parse/tokenization error
 */
static int tokenize_command(const char *cmd_str, Command *cmd) {
    const char *cursor = cmd_str;
    int unmatched_quote = 0;
    char *token = next_token_quoted(&cursor, &unmatched_quote);

    while (token != NULL && cmd->argc < MAX_ARGS - 1) {
        // Check for redirection operators
        if (strcmp(token, "<") == 0) {
            // Input redirection
            free(token);
            token = next_token_quoted(&cursor, &unmatched_quote);
            if (unmatched_quote) {
                print_parse_error("Unmatched quote");
                return -1;
            }
            if (token == NULL) {
                print_parse_error("Missing filename after '<'");
                return -1;
            }
            cmd->input_file = malloc(strlen(token) + 1);
            // If allocation fails, report error and clean temporary memory
            if (cmd->input_file == NULL) {
                perror("malloc");
                free(token);
                return -1;
            }
            strcpy(cmd->input_file, token);
            free(token);
        } else if (strcmp(token, ">") == 0) {
            // Output redirection
            free(token);
            token = next_token_quoted(&cursor, &unmatched_quote);
            if (unmatched_quote) {
                print_parse_error("Unmatched quote");
                return -1;
            }
            if (token == NULL) {
                print_parse_error("Missing filename after '>'");
                return -1;
            }
            // Check if it's error redirection (2>)
            if (strlen(token) > 0 && token[0] == '2' && strlen(token) > 1 && token[1] == '>') {
                print_parse_error("Invalid operator '2>' without space");
                free(token);
                return -1;
            }
            cmd->output_file = malloc(strlen(token) + 1);
            // If allocation fails, report error and clean temporary memory
            if (cmd->output_file == NULL) {
                perror("malloc");
                free(token);
                return -1; // abort parsing
            }
            // Copy the filename token into the allocated buffer
            strcpy(cmd->output_file, token);
            free(token);
        } else if (strcmp(token, "2>") == 0) {
            // Error redirection
            free(token);
            token = next_token_quoted(&cursor, &unmatched_quote);
            if (unmatched_quote) {
                print_parse_error("Unmatched quote");
                return -1;
            }
            if (token == NULL) {
                print_parse_error("Missing filename after '2>'");
                return -1;
            }
            cmd->error_file = malloc(strlen(token) + 1);
            // If allocation fails, report error and clean temporary memory
            if (cmd->error_file == NULL) {
                perror("malloc");
                free(token);
                return -1;
            }
            strcpy(cmd->error_file, token);
            free(token);

        } else {
            // Regular argument: copy token exactly as parsed, including all backslashes
            size_t len = strlen(token);
            char *arg = malloc(len + 1);
            if (arg == NULL) {
                perror("malloc");
                free(token);
                return -1;
            }
            memcpy(arg, token, len + 1); // preserve all bytes, including backslashes
            cmd->args[cmd->argc] = arg;
            cmd->argc++;
            free(token);
        }

        token = next_token_quoted(&cursor, &unmatched_quote);
        if (unmatched_quote) {
            print_parse_error("Unmatched quote");
            return -1;
        }
    }
    
    // Null-terminate the argument array
    cmd->args[cmd->argc] = NULL;
    
    free(token);
    return 0;
}

/*
 * Extracts the next token from the command string, handling quotes and escapes.
 * Advances the cursor and sets unmatched_quote if a quote is left open.
 *
 * Parameters:
 *   cursor         - Pointer to current position in command string (updated)
 *   unmatched_quote- Set to 1 if a quote is left open, 0 otherwise
 *
 * Returns:
 *   Heap-allocated token string, or NULL if no more tokens or error.
 */
static char *next_token_quoted(const char **cursor, int *unmatched_quote) {
    
    const char *ptr = *cursor; // Current read position in the command string
    size_t max_len; // Maximum possible token size from current position
    char *token;  // Write position in token buffer
   
    size_t out_idx = 0;
    // Quote state flags while scanning one token
    int in_single = 0;
    int in_double = 0;

    // Assume quote state is valid unless proven otherwise
    *unmatched_quote = 0;

    // Skip leading whitespace before the next token
    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }

    // no more tokens
    if (*ptr == '\0') {
        *cursor = ptr;
        return NULL;
    }

    // return '<' or '>' as standalone redirection token
    if (*ptr == '<' || *ptr == '>') {
        token = malloc(2);
        if (token == NULL) {
            perror("malloc");
            return NULL;
        }
        token[0] = *ptr;
        token[1] = '\0';
        *cursor = ptr + 1;
        return token;
    }

    // return '2>' as standalone stderr redirection token
    if (*ptr == '2' && *(ptr + 1) == '>') {
        token = malloc(3);
        if (token == NULL) {
            perror("malloc");
            return NULL;
        }
        token[0] = '2';
        token[1] = '>';
        token[2] = '\0';
        *cursor = ptr + 2;
        return token;
    }

    // allocate worst-case token size (remaining input length)
    max_len = strlen(ptr);
    token = malloc(max_len + 1);
    if (token == NULL) {
        perror("malloc");
        return NULL;
    }


    // build a token while respecting quotes and escapes
    while (*ptr) {
        if (!in_single && !in_double) {
            // outside quotes, whitespace ends the token
            if (isspace((unsigned char)*ptr)) {
                break;
            }
            // outside quotes redirection starts a new token
            if (*ptr == '<' || *ptr == '>') {
                break;
            }
            if (*ptr == '2' && *(ptr + 1) == '>') {
                break;
            }
            // outside quotes: only collapse escapes for quotes and backslash itself
            if (*ptr == '\\') {
                char next = *(ptr + 1);
                if (next == '\'' || next == '"' || next == '\\') {
                    token[out_idx++] = next;
                    ptr += 2;
                } else if (next != '\0') {
                    // preserve the backslash for sequences like \n, \t, etc.
                    token[out_idx++] = *ptr;
                    token[out_idx++] = next;
                    ptr += 2;
                } else {
                    token[out_idx++] = *ptr;
                    ptr++;
                }
                continue;
            }
            // Enter single-quoted mode (quote not copied)
            if (*ptr == '\'') {
                in_single = 1;
                ptr++;
                continue;
            }
            // Enter double-quoted mode (quote not copied)
            if (*ptr == '"') {
                in_double = 1;
                ptr++;
                continue;
            }
        } else if (in_single) {
            // In single quotes, everything is literal except the closing quote
            if (*ptr == '\'') {
                in_single = 0;
                ptr++;
                continue;
            }
            // Copy everything inside single quotes
            token[out_idx++] = *ptr;
            ptr++;
            continue;
        } else if (in_double) {
            // In double quotes, only collapse escapes for double-quote and backslash itself.
            // Per POSIX, \' inside double quotes is NOT special — both \ and ' must be kept.
            if (*ptr == '\\') {
                char next = *(ptr + 1);
                if (next == '"' || next == '\\') {
                    token[out_idx++] = next;
                    ptr += 2;
                } else if (next != '\0') {
                    // preserve the backslash for sequences like \n, \t, etc.
                    token[out_idx++] = *ptr;
                    token[out_idx++] = next;
                    ptr += 2;
                } else {
                    token[out_idx++] = *ptr;
                    ptr++;
                }
                continue;
            }
            // End double-quoted mode
            if (*ptr == '"') {
                in_double = 0;
                ptr++;
                continue;
            }
            // Copy everything inside double quotes
            token[out_idx++] = *ptr;
            ptr++;
            continue;
        }

        // Copy ordinary character into token
        token[out_idx++] = *ptr;
        ptr++;
    }

    // If a quote was opened but not closed, report parse error
    if (in_single || in_double) {
        free(token);
        *unmatched_quote = 1;
        return NULL;
    }

    // Finalize token and advance cursor for next call
    token[out_idx] = '\0';
    *cursor = ptr;
    return token;
}

/*
 * Removes leading and trailing whitespace from a string (in place).
 *
 * Parameters:
 *   str - The string to trim
 *
 * Returns:
 *   Pointer to the trimmed string (may be advanced from original)
 */
static char *trim_whitespace(char *str) {
    // Skip leading whitespace
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    
    // Find end of string
    char *end = str + strlen(str) - 1;
    
    // Skip trailing whitespace
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    
    // Null-terminate
    *(end + 1) = '\0';
    
    return str;
}

/*
 * Checks for basic syntax errors in the input string (pipes at ends, double pipes, etc).
 *
 * Parameters:
 *   input     - The input string to check
 *   error_msg - Buffer to receive error message (if any)
 *
 * Returns:
 *   1 if a syntax error is found, 0 otherwise
 */
static int has_syntax_error(const char *input, char *error_msg) {
    const char *ptr = input;

    while (*ptr) {
        // Check for pipe at the beginning (ignoring leading whitespace)
        if (*ptr == '|') {
            const char *before = input;
            while (before < ptr && isspace((unsigned char)*before)) before++;
            if (before == ptr) {
                // Everything before this pipe is whitespace 
                strcpy(error_msg, "Pipe cannot be at the beginning");
                return 1;
            }
        }
        
        // Check for double pipes or pipe with no command before
        if (*ptr == '|') {
            const char *temp = ptr + 1;
            while (*temp && isspace((unsigned char)*temp)) {
                temp++;
            }
            if (*temp == '|') {
                strcpy(error_msg, "Invalid pipe operator");
                return 1;
            }
            if (*temp == '\0') {
                strcpy(error_msg, "Pipe cannot be at the end");
                return 1;
            }
        }
        
        ptr++;
    }
    
    return 0;
}

/*
 * Frees all memory allocated for a Pipeline and its contained commands.
 *
 * Parameters:
 *   pipeline - Pointer to Pipeline to free
 */
void free_pipeline(Pipeline *pipeline) {
    if (pipeline == NULL || pipeline->commands == NULL) {
        return;
    }
    
    for (int i = 0; i < pipeline->command_count; i++) {
        Command *cmd = &pipeline->commands[i];
        
        // Free arguments
        for (int j = 0; j < cmd->argc; j++) {
            free(cmd->args[j]);
        }
        
        // Free redirection files
        if (cmd->input_file != NULL) {
            free(cmd->input_file);
        }
        if (cmd->output_file != NULL) {
            free(cmd->output_file);
        }
        if (cmd->error_file != NULL) {
            free(cmd->error_file);
        }
    }
    
    free(pipeline->commands);
    pipeline->commands = NULL;
    pipeline->command_count = 0;
}
