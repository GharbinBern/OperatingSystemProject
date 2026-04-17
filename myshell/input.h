// input.h
// Declares read_input(), which reads one line from stdin for the interactive shell.

#ifndef INPUT_H
#define INPUT_H

#include <stddef.h>

// Maximum number of characters per input line, including the null terminator.
#define MAX_INPUT_SIZE 1024

// Reads one line from stdin, strips the trailing newline, and returns a
// heap-allocated string. Returns NULL on EOF or allocation failure.
// The caller must free() the returned string.
char *read_input(void);

#endif /* INPUT_H */
