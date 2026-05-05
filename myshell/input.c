#include "input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Reads one line from stdin into a fixed buffer, strips the trailing newline,
// and returns a heap-allocated copy. Returns NULL on EOF (Ctrl+D).
// Caller is responsible for freeing the returned string.
char *read_input(void) {
    char buffer[MAX_INPUT_SIZE];  // temporary storage for the raw input line

    // read one line; returns NULL when stdin reaches EOF
    if (fgets(buffer, sizeof(buffer), stdin) == NULL)
        return NULL;

    // remove the trailing '\n' that fgets leaves in the buffer
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n')
        buffer[len - 1] = '\0';

    // allocate exactly enough space for the line plus the null terminator
    char *input = malloc(strlen(buffer) + 1);
    if (input == NULL) {
        perror("malloc");
        return NULL;
    }

    strcpy(input, buffer);  // copy from stack buffer to heap
    return input;
}
