#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// This is a stub. Replace with your Phase 1 logic.
// For now, it just runs the command using popen and returns the output as a string.
// In integration, replace this with your actual shell logic.

char* execute_command(const char* cmd) {
    FILE* fp = popen(cmd, "r");
    if (!fp) {
        char* err = strdup("Error: Command not found\n");
        return err;
    }
    char* output = malloc(4096);
    if (!output) {
        pclose(fp);
        return NULL;
    }
    size_t total = 0;
    size_t n;
    while ((n = fread(output + total, 1, 4096 - total - 1, fp)) > 0) {
        total += n;
        if (total >= 4096 - 1) break;
    }
    output[total] = '\0';
    int status = pclose(fp);
    if (status == -1 || WEXITSTATUS(status) != 0) {
        free(output);
        return strdup("Error: Command not found\n");
    }
    return output;
}
