#ifndef INPUT_H
#define INPUT_H

#include <stddef.h>

#define MAX_INPUT_SIZE 1024

/**
 * Reads a line of input from the user and removes the trailing newline.
 * Returns a dynamically allocated string that must be freed by the caller.
 * Returns NULL if EOF is reached.
 */
char *read_input(void);

#endif // INPUT_H
