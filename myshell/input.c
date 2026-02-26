#include "input.h"
#include <stdio.h> // for fgets, stdin
#include <stdlib.h> //for malloc, NULL
#include <string.h> //for strlen, strcpy

char *read_input(void) {
    // fixed local buffer to first capture the user input
    char buffer[MAX_INPUT_SIZE];
    
    // Read from stdin using fgets
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        // EOF reached
        return NULL;
    }
    
    // Remove trailing newline for easy comparison and parsing
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }
    
    // Allocate memory for the input and copy it
    char *input = malloc(strlen(buffer) + 1);
    if (input == NULL) {
        perror("malloc");
        return NULL;
    }
    strcpy(input, buffer);
    
    return input;
}
