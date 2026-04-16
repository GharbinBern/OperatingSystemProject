/*
 * input.h
 *
 * Public interface for input.c. This module handles reading a single line
 * of user input from stdin for the Phase 1 interactive shell.
 */

#ifndef INPUT_H
#define INPUT_H

#include <stddef.h> /* NULL, size_t */

/* Maximum number of characters read per input line, including the null terminator. */
#define MAX_INPUT_SIZE 1024

/*
 * read_input - read one line from stdin and return it as a heap-allocated string.
 *
 * Reads up to MAX_INPUT_SIZE characters. Strips the trailing newline if present.
 * Returns NULL on EOF (Ctrl-D) or if memory allocation fails.
 * The caller is responsible for calling free() on the returned string.
 */
char *read_input(void);

#endif /* INPUT_H */
