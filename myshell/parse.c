#include "parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> // for character helpers (isspace)

// constants for pipr and redirection sym
#define PIPE_CHAR '|'
#define INPUT_REDIR '<'
#define OUTPUT_REDIR '>'
#define ERROR_REDIR '2'

// Forward declarations
static int tokenize_command(const char *cmd_str, Command *cmd);
static char *next_token_quoted(const char **cursor, int *unmatched_quote);
static char *trim_whitespace(char *str);
static int has_syntax_error(const char *input, char *error_msg);

// prints parser errors to stderr with "Error: ..." format
void print_parse_error(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
}

Pipeline parse_input(const char *input) {
    Pipeline pipeline = {NULL, 0};
    char error_msg[256];
    
    // check for empty input
    if (input == NULL || strlen(input) == 0) {
        pipeline.command_count = 0;
        return pipeline;
    }
    
    // check for syntax errors
    if (has_syntax_error(input, error_msg)) {
        print_parse_error(error_msg);
        pipeline.command_count = -1;
        return pipeline;
    }
    
    // create a copy of input to avoid modifying the original
    char *input_copy = malloc(strlen(input) + 1);
    if (input_copy == NULL) {
        perror("malloc");
        pipeline.command_count = -1;
        return pipeline;
    }
    strcpy(input_copy, input);
    
    // split by pipes
    char *pipe_saveptr = NULL;
    char *cmd_token = strtok_r(input_copy, "|", &pipe_saveptr);
    
    // count the number of commands
    int cmd_count = 0;
    while (cmd_token != NULL) {
        // Trim whitespace and check if command is empty
        char *trimmed = trim_whitespace(cmd_token);
        if (strlen(trimmed) > 0) {
            cmd_count++;
        }
        cmd_token = strtok_r(NULL, "|", &pipe_saveptr);
    }
    //report for empty command
    if (cmd_count == 0) {
        print_parse_error("Empty command");
        free(input_copy);
        pipeline.command_count = -1;
        return pipeline;
    }
    
    // allocate memory for commands
    pipeline.commands = malloc(cmd_count * sizeof(Command));
    if (pipeline.commands == NULL) {
        perror("malloc");
        free(input_copy);
        pipeline.command_count = -1;
        return pipeline;
    }
    
    // Re-parse to fill in the commands
    strcpy(input_copy, input);
    pipe_saveptr = NULL;
    cmd_token = strtok_r(input_copy, "|", &pipe_saveptr);
    
    int cmd_idx = 0;
    while (cmd_token != NULL) {
        char *trimmed = trim_whitespace(cmd_token);
        if (strlen(trimmed) > 0) {
            // Initialize command
            Command *cmd = &pipeline.commands[cmd_idx];
            cmd->argc = 0;
            cmd->input_file = NULL;
            cmd->output_file = NULL;
            cmd->error_file = NULL;
            
            // Tokenize and parse the command
            // if tokenization fails (missing file after >, <, 2>) return command_count = -1 so execution will not run
            if (tokenize_command(trimmed, cmd) == -1) {
                free(input_copy);
                free_pipeline(&pipeline);
                pipeline.command_count = -1;
                return pipeline;
            }
            
            // Check if command is empty after parsing
            if (cmd->argc == 0) {
                print_parse_error("Empty command between pipes");
                free(input_copy);
                free_pipeline(&pipeline);
                pipeline.command_count = -1;
                return pipeline;
            }
            
            cmd_idx++;
        }
        cmd_token = strtok_r(NULL, "|", &pipe_saveptr);
    }
    
    pipeline.command_count = cmd_count;
    free(input_copy);
    return pipeline;
}

// Tokenizes a single command and extracts arguments and redirections
// Returns 0 on success, -1 on parse/tokenization error
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
            // Regular argument
            char *arg = malloc(strlen(token) + 1);
            if (arg == NULL) {
                perror("malloc");
                free(token);
                return -1;
            }
            strcpy(arg, token);
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
            // outside quotes backslash escapes next character
            if (*ptr == '\\') {
                if (*(ptr + 1) != '\0') {
                    token[out_idx++] = *(ptr + 1);
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
            // End single-quoted mode
            if (*ptr == '\'') {
                in_single = 0;
                ptr++;
                continue;
            }
        } else {
            // In double quotes, allow backslash escaping
            if (*ptr == '\\') {
                if (*(ptr + 1) != '\0') {
                    token[out_idx++] = *(ptr + 1);
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

// Removes leading and trailing whitespace from a string
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

// Checks for syntax errors in the input
static int has_syntax_error(const char *input, char *error_msg) {
    const char *ptr = input;
    
    while (*ptr) {
        // Check for pipe at the beginning
        if (*ptr == '|' && ptr == input) {
            strcpy(error_msg, "Pipe cannot be at the beginning");
            return 1;
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
