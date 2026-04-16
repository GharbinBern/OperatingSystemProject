// server.c
// Multithreaded TCP shell server. Creates one thread per client connection.
// Each thread handles all communication and command execution for its client.

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

#define PORT        3000   // TCP port the server listens on
#define BUFFER_SIZE 4096   // Maximum size of one incoming command

// Tracks how many clients have connected since the server started.
// Protected by client_num_sem to avoid duplicate numbers.
static int client_counter = 0;

// Binary semaphore (initialized to 1) used as a mutex to protect
// client_counter and thread_counter from concurrent increments.
static sem_t client_num_sem;

// Holds all information a thread needs about its assigned client.
// Allocated on the heap in main() and freed by the thread before it exits.
typedef struct {
    int client_fd;                  // socket file descriptor for this client
    struct sockaddr_in client_addr; // client IP address and port
    int client_num;                 // client sequence number (1, 2, 3 ...)
    int thread_num;                 // thread sequence number (1, 2, 3 ...)
} client_info_t;

// Entry point for each per-client thread.
// Receives commands, executes them via execute_command(), and sends responses.
// Runs until the client sends "exit" or disconnects.
void *ThreadFunction(void *arg) {
    // Unpack the client info struct passed from main().
    client_info_t *client_info = (client_info_t *)arg;
    int client_fd   = client_info->client_fd;
    struct sockaddr_in client_addr = client_info->client_addr;
    int client_num  = client_info->client_num;
    int thread_num  = client_info->thread_num;

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    // Convert the binary IP address to a printable string for log messages.
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);

    // Log the new connection with IP, port, client number, and thread number.
    printf("[INFO] Client #%d connected from %s:%d. Assigned to Thread-%d.\n",
           client_num, client_ip, client_port, thread_num);
    fflush(stdout);

    // Main loop: receive a command, execute it, send the response, repeat.
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);

        // Block until the client sends data.
        // recv() returns 0 on clean disconnect or -1 on error.
        bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_read <= 0) {
            if (bytes_read == 0)
                printf("[INFO] Client #%d disconnected.\n", client_num);
            else
                fprintf(stderr, "[ERROR] [Client #%d - %s:%d] recv failed: %s\n",
                        client_num, client_ip, client_port, strerror(errno));
            break;
        }

        // Null-terminate the received data so it can be used as a string.
        buffer[bytes_read] = '\0';

        // Strip any trailing newline before logging or executing.
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n')
            buffer[--len] = '\0';

        // Log every received command regardless of what it is.
        printf("[RECEIVED] [Client #%d - %s:%d] Received command: \"%s\"\n",
               client_num, client_ip, client_port, buffer);
        fflush(stdout);

        // If the client sends "exit", log the disconnect and close the connection.
        // The client is blocking on recv() waiting for the socket to close.
        if (strcmp(buffer, "exit") == 0) {
            printf("[INFO] [Client #%d - %s:%d] Client requested disconnect. Closing connection.\n",
                   client_num, client_ip, client_port);
            printf("[INFO] Client #%d disconnected.\n", client_num);
            fflush(stdout);
            break;
        }

        // Log that the command is about to be executed.
        printf("[EXECUTING] [Client #%d - %s:%d] Executing command: \"%s\"\n",
               client_num, client_ip, client_port, buffer);
        fflush(stdout);

        // execute_command() forks a child, runs the command, captures all output
        // through a pipe, and returns a heap-allocated string. Caller must free it.
        char *output = execute_command(buffer);

        if (output == NULL || strstr(output, "Error:") != NULL) {
            // NULL means an internal error (pipe/fork failed).
            // A string containing "Error:" means the command was not found.
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
            // Command succeeded but produced no output (e.g., mkdir, cd).
            // Send a newline so the client prompt returns instead of blocking.
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
            // Command succeeded and produced output. Forward it to the client.
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

        free(output);  // Free the string returned by execute_command().
        printf("\n");   // Blank line between log entries for readability.
    }

    // Close the client socket and free the per-client struct before exiting.
    close(client_fd);
    free(client_info);
    pthread_exit(NULL);
    return NULL;
}

// Sets up the listening socket and runs the accept loop.
// For each incoming connection, assigns a client number, spawns a thread,
// and detaches it so it cleans up automatically when it finishes.
int main(void) {
    int server_fd;
    int opt = 1;
    struct sockaddr_in address;
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    // Initialize the semaphore to 1 so it works as a binary mutex.
    // Second argument 0 means it is shared between threads of this process.
    if (sem_init(&client_num_sem, 0, 1) != 0) {
        perror("sem_init failed");
        exit(EXIT_FAILURE);
    }

    // Create the TCP server socket. AF_INET = IPv4, SOCK_STREAM = TCP.
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // SO_REUSEADDR lets the server bind immediately after a restart
    // without waiting for the port to leave TIME_WAIT state.
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Configure the address: listen on all interfaces at the given port.
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Backlog of 5 allows up to 5 pending connections to queue before accept().
    if (listen(server_fd, 5) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("[INFO] Server started, waiting for client connections...\n");
    fflush(stdout);

    int thread_counter = 0;  // Incremented alongside client_counter for Thread-N labels.

    // Accept loop: one iteration per incoming client connection.
    while (1) {
        // Block until a client connects. Returns a new socket for that client.
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (client_fd < 0) {
            // accept() can fail transiently (e.g., interrupted by a signal).
            fprintf(stderr, "[ERROR] accept failed: %s\n", strerror(errno));
            continue;
        }

        // Increment counters inside the semaphore so no two clients share a number.
        int client_num, thread_num;
        sem_wait(&client_num_sem);
        client_num  = ++client_counter;
        thread_num  = ++thread_counter;
        sem_post(&client_num_sem);

        // Allocate the client info struct on the heap. The thread will free it.
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

        // Create the thread. On failure, clean up and keep accepting other clients.
        pthread_t thread;
        if (pthread_create(&thread, NULL, ThreadFunction, (void *)client_info) != 0) {
            fprintf(stderr, "[ERROR] pthread_create failed: %s\n", strerror(errno));
            close(client_fd);
            free(client_info);
            continue;
        }

        // Detach the thread so its resources are freed automatically when it exits.
        pthread_detach(thread);
    }

    // Unreachable during normal operation.
    close(server_fd);
    sem_destroy(&client_num_sem);
    return 0;
}
