/*
 * server.c
 *
 * Multithreaded TCP shell server for Phase 3.
 *
 * The server creates one listening socket in main() and loops on accept().
 * Each time a client connects, a new POSIX thread is spawned to handle all
 * communication with that client. The thread receives commands, passes them
 * to execute_command(), and sends the output back. It runs until the client
 * types "exit" or disconnects.
 *
 * A semaphore is used to protect the global client counter so that each
 * client gets a unique number even when connections arrive at the same time.
 * Threads are detached right after creation so they free their own resources
 * when they finish, without needing a pthread_join() call.
 *
 * Log message format used throughout (as required by Phase 3):
 *   [INFO]      - server lifecycle events (startup, connect, disconnect)
 *   [RECEIVED]  - command received from a client
 *   [EXECUTING] - command is being executed
 *   [ERROR]     - command not found or internal failure
 *   [OUTPUT]    - response being sent back to the client
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

#define PORT        3000    /* TCP port the server listens on */
#define BUFFER_SIZE 4096    /* Maximum size for one incoming command */

/*
 * client_counter keeps a running total of clients that have connected during
 * this server session. Each new client receives the next incremented value,
 * so log messages consistently show "Client #1", "Client #2", and so on.
 * Access to this variable is guarded by client_num_sem.
 */
static int client_counter = 0;

/*
 * client_num_sem is used as a mutex to protect client_counter and
 * thread_counter. Using a semaphore (initialized to 1) ensures that the
 * counter increments are atomic even when multiple clients connect at once.
 */
static sem_t client_num_sem;

/*
 * client_info_t holds all the information a thread needs about its client.
 * An instance is allocated on the heap in main() and passed to the thread
 * through pthread_create(). The thread frees this struct before exiting.
 *
 * Fields:
 *   client_fd   - the socket file descriptor for this client connection
 *   client_addr - the client's IP address and port number
 *   client_num  - the client's assigned sequence number (1, 2, 3, ...)
 *   thread_num  - the thread's sequence number (matches client_num here)
 */
typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
    int client_num;
    int thread_num;
} client_info_t;

/*
 * ThreadFunction is the entry point for each per-client thread. It handles
 * the full lifecycle of one client connection.
 *
 * The function unpacks the client_info_t argument, logs the connection, then
 * enters a loop where it receives a command, executes it, and sends the
 * response back. The loop ends when the client sends "exit" or disconnects.
 * After the loop, the client socket is closed and the argument struct is freed.
 *
 * Parameters:
 *   arg - pointer to a heap-allocated client_info_t (thread takes ownership)
 *
 * Returns:
 *   NULL (the return value is not used because the thread is detached)
 */
void *ThreadFunction(void *arg) {
    /* Unpack the client information passed from main(). */
    client_info_t *client_info = (client_info_t *)arg;
    int client_fd              = client_info->client_fd;
    struct sockaddr_in client_addr = client_info->client_addr;
    int client_num             = client_info->client_num;
    int thread_num             = client_info->thread_num;

    char buffer[BUFFER_SIZE]; /* Buffer for receiving commands from the client */
    ssize_t bytes_read;

    /* Convert the binary IP address to a printable dotted-decimal string. */
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);

    /* Log the new connection with IP, port, client number, and thread number. */
    printf("[INFO] Client #%d connected from %s:%d. Assigned to Thread-%d.\n",
           client_num, client_ip, client_port, thread_num);
    fflush(stdout);

    /* Main receive-execute-respond loop for this client. */
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);

        /*
         * recv() blocks until the client sends data. It returns the number of
         * bytes received, 0 if the client closed the connection, or -1 on error.
         */
        bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_read <= 0) {
            /* Differentiate between a clean disconnect and a socket error. */
            if (bytes_read == 0) {
                printf("[INFO] Client #%d disconnected.\n", client_num);
            } else {
                fprintf(stderr, "[ERROR] [Client #%d - %s:%d] recv failed: %s\n",
                        client_num, client_ip, client_port, strerror(errno));
            }
            break;
        }

        /* Null-terminate the received data so it can be used as a string. */
        buffer[bytes_read] = '\0';

        /*
         * The client strips its own newline before sending, but strip again
         * here as a safety measure to keep the command string clean for
         * execute_command() and for the log messages.
         */
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[--len] = '\0';
        }

        /* Log every command as soon as it is received. */
        printf("[RECEIVED] [Client #%d - %s:%d] Received command: \"%s\"\n",
               client_num, client_ip, client_port, buffer);
        fflush(stdout);

        /*
         * If the command is "exit", the client wants to disconnect gracefully.
         * Log the disconnect, break out of the loop, and let the cleanup below
         * close the socket. The client is blocking on recv() waiting for the
         * socket to close before printing "Disconnected from server.".
         */
        if (strcmp(buffer, "exit") == 0) {
            printf("[INFO] [Client #%d - %s:%d] Client requested disconnect. Closing connection.\n",
                   client_num, client_ip, client_port);
            printf("[INFO] Client #%d disconnected.\n", client_num);
            fflush(stdout);
            break;
        }

        /* Log that execution is starting before calling execute_command(). */
        printf("[EXECUTING] [Client #%d - %s:%d] Executing command: \"%s\"\n",
               client_num, client_ip, client_port, buffer);
        fflush(stdout);

        /*
         * execute_command() forks a child, runs the command, captures all
         * output through a pipe, and returns it as a heap-allocated string.
         * The caller (this thread) is responsible for freeing that string.
         */
        char *output = execute_command(buffer);

        if (output == NULL || strstr(output, "Error:") != NULL) {
            /*
             * A NULL return indicates an internal failure (pipe or fork error).
             * A string containing "Error:" was written to stderr by the child
             * process in execute.c, meaning the command was not found or failed
             * to execute. In both cases, send the error message to the client.
             */
            const char *err_msg = (output != NULL) ? output : "Command not found\n";

            fprintf(stderr, "[ERROR] [Client #%d - %s:%d] Command not found: \"%s\"\n",
                    client_num, client_ip, client_port, buffer);
            printf("[OUTPUT] [Client #%d - %s:%d] Sending error message to client:\n\"%s\"\n",
                   client_num, client_ip, client_port, err_msg);
            fflush(stdout);

            if (send(client_fd, err_msg, strlen(err_msg), 0) < 0) {
                fprintf(stderr, "[ERROR] [Client #%d - %s:%d] Failed to send error response: %s\n",
                        client_num, client_ip, client_port, strerror(errno));
                free(output);
                break;
            }

        } else if (strlen(output) == 0) {
            /*
             * The command ran successfully but produced no output (for example,
             * commands like "cd" or "mkdir"). Send a newline so the client
             * prompt returns immediately instead of hanging on the next recv().
             */
            const char *no_output = "\n";

            printf("[OUTPUT] [Client #%d - %s:%d] Sending output to client: (empty)\n",
                   client_num, client_ip, client_port);
            fflush(stdout);

            if (send(client_fd, no_output, strlen(no_output), 0) < 0) {
                fprintf(stderr, "[ERROR] [Client #%d - %s:%d] Failed to send empty output: %s\n",
                        client_num, client_ip, client_port, strerror(errno));
                free(output);
                break;
            }

        } else {
            /* The command succeeded and produced output. Forward it to the client. */
            printf("[OUTPUT] [Client #%d - %s:%d] Sending output to client:\n%s\n",
                   client_num, client_ip, client_port, output);
            fflush(stdout);

            if (send(client_fd, output, strlen(output), 0) < 0) {
                fprintf(stderr, "[ERROR] [Client #%d - %s:%d] Failed to send output: %s\n",
                        client_num, client_ip, client_port, strerror(errno));
                free(output);
                break;
            }
        }

        free(output); /* Free the heap string returned by execute_command(). */
        printf("\n");  /* Blank line between log entries for readability. */
    }

    /* Close the client socket and free the per-client struct before exiting. */
    close(client_fd);
    free(client_info);

    pthread_exit(NULL);
    return NULL; /* Unreachable, but silences compiler warnings. */
}

/*
 * main sets up the server socket and runs the accept loop. For each incoming
 * connection it assigns a client number, allocates a client_info_t, creates a
 * thread to handle the client, and immediately detaches that thread.
 *
 * Steps:
 *   1. Initialize the semaphore that protects the client counter.
 *   2. Create a TCP socket and set SO_REUSEADDR to allow quick restarts.
 *   3. Bind to all network interfaces on PORT and start listening.
 *   4. Loop forever: accept a connection, spin up a thread, detach it.
 */
int main(void) {
    int server_fd;
    int opt = 1;
    struct sockaddr_in address;
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    /*
     * Initialize the semaphore to 1 so it works as a binary mutex.
     * The second argument (0) scopes it to threads within this process.
     */
    if (sem_init(&client_num_sem, 0, 1) != 0) {
        perror("sem_init failed");
        exit(EXIT_FAILURE);
    }

    /*
     * Create the server socket. AF_INET selects IPv4; SOCK_STREAM selects
     * TCP for a reliable, connection-oriented transport.
     */
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    /*
     * SO_REUSEADDR lets the server bind to a port that is still in TIME_WAIT
     * state after a recent shutdown, which avoids the "Address already in use"
     * error when restarting the server quickly.
     */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* Configure the address structure: accept connections on any interface. */
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);

    /* Bind the socket to the configured address and port. */
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /*
     * Start listening. The backlog of 5 means up to 5 connection requests
     * can queue while the main thread is busy processing a previous accept().
     */
    if (listen(server_fd, 5) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("[INFO] Server started, waiting for client connections...\n");
    fflush(stdout);

    int thread_counter = 0; /* Tracks thread numbers alongside client numbers. */

    /* Accept loop: runs indefinitely, one iteration per incoming connection. */
    while (1) {
        /*
         * accept() blocks until a client connects. It returns a new socket
         * dedicated to that client and populates client_addr with the client's
         * IP address and port number.
         */
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (client_fd < 0) {
            /* accept() can fail transiently (e.g., interrupted by a signal). */
            fprintf(stderr, "[ERROR] accept failed: %s\n", strerror(errno));
            continue;
        }

        /*
         * Increment the counters inside a semaphore-protected section so
         * no two threads ever share the same client or thread number,
         * even when multiple clients connect at nearly the same time.
         */
        int client_num, thread_num;
        sem_wait(&client_num_sem);
        client_num  = ++client_counter;
        thread_num  = ++thread_counter;
        sem_post(&client_num_sem);

        /*
         * Allocate the client info struct on the heap so it remains valid
         * after this loop iteration. The thread that receives it will free it.
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
         * Create a new thread for this client. If thread creation fails,
         * close the socket and free the struct, then continue accepting
         * other clients rather than crashing the whole server.
         */
        pthread_t thread;
        if (pthread_create(&thread, NULL, ThreadFunction, (void *)client_info) != 0) {
            fprintf(stderr, "[ERROR] pthread_create failed: %s\n", strerror(errno));
            close(client_fd);
            free(client_info);
            continue;
        }

        /*
         * Detach the thread so its resources are released automatically
         * when it exits. The main thread never calls pthread_join() because
         * it must remain available to accept new connections.
         */
        pthread_detach(thread);
    }

    /* These lines are never reached during normal operation. */
    close(server_fd);
    sem_destroy(&client_num_sem);
    return 0;
}
