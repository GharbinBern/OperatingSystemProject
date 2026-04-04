#include "input.h"
#include <stdio.h> // for fgets, stdin
#include <stdlib.h> //for malloc, NULL
#include <string.h> //for strlen, strcpy


/*
 * Reads a line of input from the user (stdin), removes the trailing newline, and returns
 * a dynamically allocated string. Handles EOF (Ctrl+D) by returning NULL.
 *
 * Main logic steps:
 *   1. Read a line from stdin into a fixed-size buffer using fgets().
 *   2. If EOF is reached, return NULL.
 *   3. Remove the trailing newline character, if present.
 *   4. Allocate memory for the input string and copy the buffer.
 *   5. Return the dynamically allocated string (caller must free).
 */
char *read_input(void) {
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
