#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input.h"
#include "parse.h"

/*
 * main.c 
 *
 * This is the entry point for the interactive shell. It repeatedly prompts the user for input,
 * parses the input into a pipeline of commands, and executes them. It handles built-in commands
 * (like "exit"), empty input, and error reporting. All resources are properly freed after each command.
 *
 * Main logic steps:
 *   1. Print the "$" prompt and flush stdout to ensure prompt visibility.
 *   2. Read a line of input from the user (dynamically allocated).
 *   3. If EOF (Ctrl+D), print a newline and exit.
 *   4. If the input is "exit", free memory and exit.
 *   5. Skip empty input lines.
 *   6. Parse the input into a Pipeline structure (using parse_input()).
 *   7. If parsing fails, print error and continue.
 *   8. If no commands, continue.
 *   9. Execute the pipeline (using execute_pipeline()).
 *  10. Free all resources (pipeline and input string) before next iteration.
 */

int main(void) {
    char *input; // Dynamically allocated command line input
    Pipeline pipeline; // Parsed pipeline of commands

    while (1) {
        // Print prompt and flush to ensure it appears immediately
        printf("$ ");
        fflush(stdout);

        // Read input from user
        input = read_input();

        // Handle EOF (Ctrl+D): print newline and exit
        if (input == NULL) {
            printf("\n");
            break;
        }

        // Handle built-in "exit" command
        if (strcmp(input, "exit") == 0) {
            free(input);
            break;
        }

        // Skip empty input lines
        if (strlen(input) == 0) {
            free(input);
            continue;
        }

        // Parse input into a pipeline of commands
        pipeline = parse_input(input);

        // If parsing failed, error already printed; skip execution
        if (pipeline.command_count == -1) {
            free(input);
            continue;
        }

        // If no commands (e.g., whitespace), skip
        if (pipeline.command_count == 0) {
            free(input);
            continue;
        }

        // Execute the parsed pipeline (handles pipes, redirections, etc.)
        int exec_status = execute_pipeline(&pipeline);
        if (exec_status == -1) {
            fprintf(stderr, "Error: Failed to execute command(s).\n");
        }

        // Free all resources for this command
        free_pipeline(&pipeline);
        free(input);
    }
    return 0;
}
