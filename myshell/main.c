#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input.h"
#include "parse.h"
#include "execute.h"

int main(void) {
    char    *input;    // dynamically allocated line from the user
    Pipeline pipeline; // parsed representation of the command

    while (1) {
        printf("$ ");      // print the shell prompt
        fflush(stdout);    // flush so the prompt appears before user types

        input = read_input();  // read one line from stdin

        // Ctrl+D / EOF: exit cleanly
        if (input == NULL) {
            printf("\n");
            break;
        }

        // built-in "exit": free memory and quit
        if (strcmp(input, "exit") == 0) {
            free(input);
            break;
        }

        // skip blank lines without printing anything
        if (strlen(input) == 0) {
            free(input);
            continue;
        }

        // parse the raw string into a Pipeline of Commands
        pipeline = parse_input(input);

        // parse error: message already printed by parse_input
        if (pipeline.command_count == -1) {
            free(input);
            continue;
        }

        // nothing to execute (e.g., whitespace-only input)
        if (pipeline.command_count == 0) {
            free(input);
            continue;
        }

        // execute the pipeline; handles pipes, redirections, forks, and waits
        int exec_status = execute_pipeline(&pipeline);
        if (exec_status == -1)
            fprintf(stderr, "Error: Failed to execute command(s).\n");

        // release memory before next iteration
        free_pipeline(&pipeline);
        free(input);
    }
    return 0;
}
