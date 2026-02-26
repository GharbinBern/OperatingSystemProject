#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input.h"
#include "parse.h"
#include "execute.h"

int main(void) {
    char *input; //to hold dynamically read command line
    Pipeline pipeline; //struct to hold parsed commands
    
    while (1) {
        // loop to (prompt > read > parse > execute)
        // print the prompt
        printf("$ ");
        fflush(stdout); //to force prompt to appear immediately
        
        // Read input
        input = read_input();
        
        // check for EOF (Ctrl+D) and print newline and exit  
        if (input == NULL) {
            printf("\n");
            break;
        }
        
        // check for exit command
        if (strcmp(input, "exit") == 0) {
            free(input); //frees memory
            break;
        }
        
        // skip empty input
        if (strlen(input) == 0) {
            free(input);
            continue;
        }
        
        // parse the input
        pipeline = parse_input(input);
        
        // check for parsing errors
        if (pipeline.command_count == -1) {
            // Error message already printed by parser
            free(input);
            continue;
        }
        
        // skip if no commands (empty input after trimming)
        if (pipeline.command_count == 0) {
            free(input);
            continue;
        }
        
        // Execute the pipeline (also executor handles its own error messages)
        execute_pipeline(&pipeline);
        
        // Free the pipeline
        free_pipeline(&pipeline);
        free(input);
    }
    
    return 0;
}
