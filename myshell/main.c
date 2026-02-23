#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input.h"
#include "parse.h"
#include "execute.h"

int main(void) {
    char *input;
    Pipeline pipeline;
    
    while (1) {
        // Print the prompt
        printf("$ ");
        fflush(stdout);
        
        // Read input
        input = read_input();
        
        // Check for EOF (Ctrl+D)
        if (input == NULL) {
            printf("\n");
            break;
        }
        
        // Check for exit command
        if (strcmp(input, "exit") == 0) {
            free(input);
            break;
        }
        
        // Skip empty input
        if (strlen(input) == 0) {
            free(input);
            continue;
        }
        
        // Parse the input
        pipeline = parse_input(input);
        
        // Check for parsing errors
        if (pipeline.command_count == -1) {
            // Error message already printed by parser
            free(input);
            continue;
        }
        
        // Skip if no commands (empty input after trimming)
        if (pipeline.command_count == 0) {
            free(input);
            continue;
        }
        
        // Execute the pipeline
        if (execute_pipeline(&pipeline) == -1) {
            // Error message should be printed by executor
        }
        
        // Free the pipeline
        free_pipeline(&pipeline);
        free(input);
    }
    
    return 0;
}
