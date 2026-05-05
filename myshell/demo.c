// demo.c
// Test program for the Phase 4 scheduler.
// Usage: ./demo N
// Prints one line per second for N seconds (lines 0 through N).
// The scheduler uses N as the predicted burst time.
// SIGSTOP pauses it mid-loop; SIGCONT resumes it from where it stopped.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s N\n", argv[0]);
        return 1;
    }

    int n = atoi(argv[1]);
    if (n <= 0) {
        fprintf(stderr, "Error: N must be a positive integer\n");
        return 1;
    }

    // Print iteration i, sleep 1 second, repeat until i == n.
    // Total wall-clock time = n seconds, matching the burst time value.
    // fflush ensures each line reaches the pipe immediately.
    for (int i = 0; i <= n; i++) {
        printf("Demo %d/%d\n", i, n);
        fflush(stdout);
        if (i < n)
            sleep(1);
    }

    return 0;
}
