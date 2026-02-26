#include "parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define PIPE_CHAR '|'
#define INPUT_REDIR '<'
#define OUTPUT_REDIR '>'
#define ERROR_REDIR '2'

// Forward declarations
static int tokenize_command(const char *cmd_str, Command *cmd);
static char *trim_whitespace(char *str);
static int has_syntax_error(const char *input, char *error_msg);

void print_parse_error(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
}

Pipeline parse_input(const char *input) {
    Pipeline pipeline = {NULL, 0};
    char error_msg[256];
    
    // Check for empty input
    if (input == NULL || strlen(input) == 0) {
        pipeline.command_count = 0;
        return pipeline;
    }
    
    // Check for syntax errors
    if (has_syntax_error(input, error_msg)) {
        print_parse_error(error_msg);
        pipeline.command_count = -1;
        return pipeline;
    }
    
    // Create a copy of input to avoid modifying the original
    char *input_copy = malloc(strlen(input) + 1);
    if (input_copy == NULL) {
        perror("malloc");
        pipeline.command_count = -1;
        return pipeline;
    }
    strcpy(input_copy, input);
    
    // Split by pipes
    char *pipe_saveptr = NULL;
    char *cmd_token = strtok_r(input_copy, "|", &pipe_saveptr);
    
    // Count the number of commands
    int cmd_count = 0;
    while (cmd_token != NULL) {
        // Trim whitespace and check if command is empty
        char *trimmed = trim_whitespace(cmd_token);
        if (strlen(trimmed) > 0) {
            cmd_count++;
        }
        cmd_token = strtok_r(NULL, "|", &pipe_saveptr);
    }
    
    if (cmd_count == 0) {
        print_parse_error("Empty command");
        free(input_copy);
        pipeline.command_count = -1;
        return pipeline;
    }
    
    // Allocate memory for commands
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
            
            // Tokenize and parse the command.
            // IMPORTANT FIX: if tokenization fails (e.g., missing file after >, <, 2>),
            // parse_input now returns command_count = -1 so execution will not run.
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

// Tokenizes a single command and extracts arguments and redirections.
// Returns 0 on success, -1 on parse/tokenization error.
static int tokenize_command(const char *cmd_str, Command *cmd) {
    char *cmd_copy = malloc(strlen(cmd_str) + 1);
    if (cmd_copy == NULL) {
        perror("malloc");
        return -1;
    }
    strcpy(cmd_copy, cmd_str);
    
    char *saveptr = NULL;
    char *token = strtok_r(cmd_copy, " \t", &saveptr);
    
    while (token != NULL && cmd->argc < MAX_ARGS - 1) {
        // Check for redirection operators
        if (strcmp(token, "<") == 0) {
            // Input redirection
            token = strtok_r(NULL, " \t", &saveptr);
            if (token == NULL) {
                print_parse_error("Missing filename after '<'");
                free(cmd_copy);
                return -1;
            }
            cmd->input_file = malloc(strlen(token) + 1);
            if (cmd->input_file == NULL) {
                perror("malloc");
                free(cmd_copy);
                return -1;
            }
            strcpy(cmd->input_file, token);
        } else if (strcmp(token, ">") == 0) {
            // Output redirection
            token = strtok_r(NULL, " \t", &saveptr);
            if (token == NULL) {
                print_parse_error("Missing filename after '>'");
                free(cmd_copy);
                return -1;
            }
            // Check if it's error redirection (2>)
            if (strlen(token) > 0 && token[0] == '2' && strlen(token) > 1 && token[1] == '>') {
                print_parse_error("Invalid operator '2>' without space");
                free(cmd_copy);
                return -1;
            }
            cmd->output_file = malloc(strlen(token) + 1);
            if (cmd->output_file == NULL) {
                perror("malloc");
                free(cmd_copy);
                return -1;
            }
            strcpy(cmd->output_file, token);
        } else if (strcmp(token, "2>") == 0) {
            // Error redirection
            token = strtok_r(NULL, " \t", &saveptr);
            if (token == NULL) {
                print_parse_error("Missing filename after '2>'");
                free(cmd_copy);
                return -1;
            }
            cmd->error_file = malloc(strlen(token) + 1);
            if (cmd->error_file == NULL) {
                perror("malloc");
                free(cmd_copy);
                return -1;
            }
            strcpy(cmd->error_file, token);
        } else {
            // Regular argument
            char *arg = malloc(strlen(token) + 1);
            if (arg == NULL) {
                perror("malloc");
                free(cmd_copy);
                return -1;
            }
            strcpy(arg, token);
            cmd->args[cmd->argc] = arg;
            cmd->argc++;
        }
        
        token = strtok_r(NULL, " \t", &saveptr);
    }
    
    // Null-terminate the argument array
    cmd->args[cmd->argc] = NULL;
    
    free(cmd_copy);
    return 0;
}

// Removes leading and trailing whitespace from a string.
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

// Checks for syntax errors in the input.
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
