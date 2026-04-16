/*
 * server.c
 *
 * Multithreaded TCP shell server (Phase 3).
 *
 * Architecture overview:
 *   - The main thread creates a TCP listening socket and loops forever calling accept().
 *   - For every new client connection, a dedicated POSIX thread (pthread) is spawned.
 *   - Each thread owns the full lifecycle of its client: receive command → execute via
 *     execute_command() → send response → repeat until "exit" or disconnect.
 *   - A semaphore protects the shared client/thread counter so that client numbers
 *     remain unique even when multiple connections arrive nearly simultaneously.
 *   - Threads are detached immediately after creation so their resources are reclaimed
 *     automatically when they exit, without needing a join().
 *
 * Logging format (matches the required Phase 3 output):
 *   [INFO]      server lifecycle events (start, connect, disconnect)
 *   [RECEIVED]  command received from a client
 *   [EXECUTING] command is about to be executed
 *   [ERROR]     command not found or internal error
 *   [OUTPUT]    response being sent back to a client
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include "shell.h"

/* ── Constants ──────────────────────────────────────────────────────────── */

#define PORT        3000   /* TCP port the server listens on                 */
#define BUFFER_SIZE 4096   /* Maximum bytes for one incoming command message */

/* ── Shared state (protected by client_num_sem) ─────────────────────────── */

/*
 * client_counter tracks the total number of clients ever connected during
 * this server run.  Each new client gets an incremented value so log messages
 * always say "Client #1", "Client #2", etc.  thread_counter in main() mirrors
 * this for the "Thread-N" label.
 */
static int client_counter = 0;

/*
 * Semaphore used as a mutex: sem_wait/sem_post ensure that the increment of
 * client_counter and thread_counter in main() is atomic, so no two threads
 * ever receive the same client number even under heavy concurrent load.
 */
static sem_t client_num_sem;

/* ── Per-client data passed to each thread ──────────────────────────────── */

/*
 * client_info_t bundles everything a newly spawned thread needs to know about
 * its client.  A heap-allocated instance is passed through pthread_create()
 * and freed by the thread itself before it exits.
 *
 * Fields:
 *   client_fd   – connected socket file descriptor for this client
 *   client_addr – sockaddr_in holding the client's IP address and port
 *   client_num  – human-readable client sequence number (1, 2, 3, …)
 *   thread_num  – thread sequence number (mirrors client_num here)
 */
typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
    int client_num;
    int thread_num;
} client_info_t;

/* ── Thread entry point ─────────────────────────────────────────────────── */

/*
 * ThreadFunction – executed by each per-client pthread.
 *
 * Steps performed:
 *   1. Extract client metadata from the heap-allocated client_info_t arg.
 *   2. Log the new connection with IP, port, client number, and thread number.
 *   3. Enter the receive-execute-respond loop:
 *        a. recv() a command from the client.
 *        b. If the command is "exit", log the graceful disconnect and break.
 *        c. Otherwise pass the command to execute_command() and send the result.
 *        d. Distinguish between success output, empty output, and error output.
 *   4. Close the client socket and free the argument struct.
 *   5. Exit the thread with pthread_exit().
 *
 * Parameters:
 *   arg – void* pointer to a heap-allocated client_info_t (ownership transferred)
 *
 * Returns:
 *   NULL (return value is unused because the thread is detached)
 */
void *ThreadFunction(void *arg) {
    /* Unpack the per-client info struct passed from main(). */
    client_info_t *client_info = (client_info_t *)arg;
    int            client_fd   = client_info->client_fd;
    struct sockaddr_in client_addr = client_info->client_addr;
    int            client_num  = client_info->client_num;
    int            thread_num  = client_info->thread_num;

    char    buffer[BUFFER_SIZE]; /* Reusable receive buffer for incoming commands */
    ssize_t bytes_read;

    /* Convert binary IP address to a human-readable dotted-decimal string. */
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);

    /* Announce the new client connection in the required [INFO] format. */
    printf("[INFO] Client #%d connected from %s:%d. Assigned to Thread-%d.\n",
           client_num, client_ip, client_port, thread_num);
    fflush(stdout);

    /* ── Main receive-execute-respond loop ───────────────────────────── */
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);

        /*
         * Block until the client sends data.  recv() returns:
         *   >0  – number of bytes received (normal case)
         *    0  – client closed the connection cleanly
         *   <0  – socket error
         */
        bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_read <= 0) {
            /* Distinguish between a clean close and a socket error. */
            if (bytes_read == 0) {
                printf("[INFO] Client #%d disconnected.\n", client_num);
            } else {
                fprintf(stderr,
                    "[ERROR] [Client #%d - %s:%d] recv failed: %s\n",
                    client_num, client_ip, client_port, strerror(errno));
            }
            break; /* Exit the loop; cleanup happens below. */
        }

        /* Null-terminate whatever was received so we can treat it as a C string. */
        buffer[bytes_read] = '\0';

        /*
         * The client appends a '\n' when it sends a command.  Strip it here so
         * that execute_command() and log messages see a clean command string.
         */
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[--len] = '\0';
        }

        /* Log every received command regardless of what it is. */
        printf("[RECEIVED] [Client #%d - %s:%d] Received command: \"%s\"\n",
               client_num, client_ip, client_port, buffer);
        fflush(stdout);

        /*
         * Special case: "exit" means the client wants to disconnect gracefully.
         * Log the disconnect request, then break out of the loop so the socket
         * is closed cleanly.  The client is waiting for the socket to close
         * before it prints "Disconnected from server.".
         */
        if (strcmp(buffer, "exit") == 0) {
            printf("[INFO] [Client #%d - %s:%d] Client requested disconnect. Closing connection.\n",
                   client_num, client_ip, client_port);
            printf("[INFO] Client #%d disconnected.\n", client_num);
            fflush(stdout);
            break;
        }

        /* Log that execution is about to start. */
        printf("[EXECUTING] [Client #%d - %s:%d] Executing command: \"%s\"\n",
               client_num, client_ip, client_port, buffer);
        fflush(stdout);

        /*
         * Delegate to execute_command() which forks, execs, captures output
         * via a pipe, and returns a heap-allocated string.
         * Caller (us) must free the returned string.
         */
        char *output = execute_command(buffer);

        if (output == NULL || strstr(output, "Error:") != NULL) {
            /*
             * NULL return means an internal error (pipe/fork failed).
             * A string starting with "Error:" means the command was not found
             * or execution failed.  In both cases, forward the error message
             * to the client and log it on the server.
             */
            const char *err_msg = (output != NULL) ? output : "Command not found\n";

            fprintf(stderr,
                "[ERROR] [Client #%d - %s:%d] Command not found: \"%s\"\n",
                client_num, client_ip, client_port, buffer);
            printf("[OUTPUT] [Client #%d - %s:%d] Sending error message to client:\n\"%s\"\n",
                   client_num, client_ip, client_port, err_msg);
            fflush(stdout);

            if (send(client_fd, err_msg, strlen(err_msg), 0) < 0) {
                fprintf(stderr,
                    "[ERROR] [Client #%d - %s:%d] Failed to send error response: %s\n",
                    client_num, client_ip, client_port, strerror(errno));
                free(output);
                break;
            }

        } else if (strlen(output) == 0) {
            /*
             * Command ran successfully but produced no output (e.g., "cd", "mkdir").
             * Send a single newline so the client prompt appears on its own line
             * instead of freezing while waiting for data.
             */
            const char *no_output = "\n";

            printf("[OUTPUT] [Client #%d - %s:%d] Sending output to client: (empty)\n",
                   client_num, client_ip, client_port);
            fflush(stdout);

            if (send(client_fd, no_output, strlen(no_output), 0) < 0) {
                fprintf(stderr,
                    "[ERROR] [Client #%d - %s:%d] Failed to send empty output: %s\n",
                    client_num, client_ip, client_port, strerror(errno));
                free(output);
                break;
            }

        } else {
            /* Normal case: command produced output — forward it to the client. */
            printf("[OUTPUT] [Client #%d - %s:%d] Sending output to client:\n%s\n",
                   client_num, client_ip, client_port, output);
            fflush(stdout);

            if (send(client_fd, output, strlen(output), 0) < 0) {
                fprintf(stderr,
                    "[ERROR] [Client #%d - %s:%d] Failed to send output: %s\n",
                    client_num, client_ip, client_port, strerror(errno));
                free(output);
                break;
            }
        }

        free(output); /* Always free the string returned by execute_command(). */
        printf("\n"); /* Blank line between log entries for readability. */
    }

    /* ── Cleanup ─────────────────────────────────────────────────────── */

    /*
     * Close the client socket.  Because the thread is detached, no join is
     * needed; the OS reclaims file descriptors and thread stack automatically.
     */
    close(client_fd);
    free(client_info); /* Free the heap struct that was allocated in main(). */

    pthread_exit(NULL);
    return NULL; /* Unreachable but satisfies the compiler warning. */
}

/* ── main ────────────────────────────────────────────────────────────────── */

/*
 * main – sets up the listening socket and accepts client connections forever.
 *
 * Steps:
 *   1. Initialize the semaphore used to protect the client counter.
 *   2. Create a TCP socket with SO_REUSEADDR so the port can be reused
 *      quickly after the server restarts.
 *   3. Bind to INADDR_ANY:PORT and start listening.
 *   4. Loop: accept() → allocate client_info_t → pthread_create() → pthread_detach().
 */
int main(void) {
    int server_fd;
    int opt = 1;
    struct sockaddr_in address;
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    /*
     * Initialise the semaphore to 1 (binary/mutex mode).
     * The second argument (0) means the semaphore is shared between threads
     * of the same process (not between processes).
     */
    if (sem_init(&client_num_sem, 0, 1) != 0) {
        perror("sem_init failed");
        exit(EXIT_FAILURE);
    }

    /*
     * AF_INET  = IPv4
     * SOCK_STREAM = TCP (reliable, connection-oriented)
     * 0 = default protocol for SOCK_STREAM (TCP)
     */
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    /*
     * SO_REUSEADDR allows the server to bind to a port that is still in
     * TIME_WAIT state (e.g., after a quick restart), preventing the
     * "Address already in use" error.
     */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* Configure the server address: listen on all interfaces at PORT. */
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);

    /* Bind the socket to the address/port. */
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /*
     * Put the socket in the listening state.  The backlog (5) is the maximum
     * number of pending connections that can queue up before accept() is called.
     */
    if (listen(server_fd, 5) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("[INFO] Server started, waiting for client connections...\n");
    fflush(stdout);

    int thread_counter = 0; /* Incremented alongside client_counter for Thread-N labels. */

    /* ── Accept loop ─────────────────────────────────────────────────── */
    while (1) {
        /*
         * accept() blocks until a client connects.  It returns a new socket
         * file descriptor dedicated to that client and fills client_addr with
         * the client's IP and port.
         */
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (client_fd < 0) {
            /* accept() may fail transiently (e.g., signal interruption); log and retry. */
            fprintf(stderr, "[ERROR] accept failed: %s\n", strerror(errno));
            continue;
        }

        /*
         * Atomically increment both counters inside the semaphore-protected
         * critical section so no two threads share a client/thread number.
         */
        int client_num, thread_num;
        sem_wait(&client_num_sem);
        client_num  = ++client_counter;
        thread_num  = ++thread_counter;
        sem_post(&client_num_sem);

        /*
         * Allocate a client_info_t on the heap so the data remains valid after
         * this loop iteration ends.  Ownership is transferred to the new thread,
         * which frees it before calling pthread_exit().
         */
        client_info_t *client_info = malloc(sizeof(client_info_t));
        if (client_info == NULL) {
            fprintf(stderr, "[ERROR] malloc failed for client_info_t: %s\n", strerror(errno));
            close(client_fd);
            continue;
        }
        client_info->client_fd   = client_fd;
        client_info->client_addr = client_addr;
        client_info->client_num  = client_num;
        client_info->thread_num  = thread_num;

        /*
         * Create the per-client thread.  ThreadFunction receives client_info
         * as its sole argument.  If thread creation fails, clean up and move on
         * rather than crashing — the server should keep accepting other clients.
         */
        pthread_t thread;
        if (pthread_create(&thread, NULL, ThreadFunction, (void *)client_info) != 0) {
            fprintf(stderr, "[ERROR] pthread_create failed: %s\n", strerror(errno));
            close(client_fd);
            free(client_info);
            continue;
        }

        /*
         * Detach the thread so it releases its resources automatically when it
         * finishes.  We never call pthread_join() because the main thread must
         * stay in the accept loop.
         */
        pthread_detach(thread);
    }

    /* Unreachable in normal operation; included for correctness. */
    close(server_fd);
    sem_destroy(&client_num_sem);
    return 0;
}
