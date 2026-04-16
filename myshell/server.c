
// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <pthread.h>
#include "shell.h"

#define PORT        3000
#define BUFFER_SIZE 4096

// Structure to pass client info to thread
typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
} client_info_t;


#include <semaphore.h>
static int client_counter = 0;
static sem_t client_num_sem;

typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
    int client_num;
    int thread_num;
} client_info_t;

void* ThreadFunction(void *arg) {
    client_info_t *client_info = (client_info_t *)arg;
    int client_fd = client_info->client_fd;
    struct sockaddr_in client_addr = client_info->client_addr;
    int client_num = client_info->client_num;
    int thread_num = client_info->thread_num;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);

    // Log client connection with IP, port, client number, and thread number
    printf("[INFO] Client #%d connected from %s:%d. Assigned to Thread-%d.\n", client_num, client_ip, client_port, thread_num);
    fflush(stdout);

    // Main loop: receive, execute, and respond to client commands
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

        // Handle client disconnect or recv error
        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                printf("[INFO] Client #%d disconnected.\n", client_num);
            } else {
                fprintf(stderr, "[ERROR] [Client #%d - %s:%d] recv failed: %s\n", client_num, client_ip, client_port, strerror(errno));
            }
            break;
        }

        buffer[bytes_read] = '\0';

        // Strip trailing newline sent by the client
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[--len] = '\0';
        }

        // Log received command with client info
        printf("[RECEIVED] [Client #%d - %s:%d] Received command: \"%s\"\n", client_num, client_ip, client_port, buffer);
        printf("[EXECUTING] [Client #%d - %s:%d] Executing command: \"%s\"\n", client_num, client_ip, client_port, buffer);
        fflush(stdout);

        // Run the command through the parser/executor
        char *output = execute_command(buffer);

        if (!output || strstr(output, "Error:") != NULL) {
            // Command failed or produced an error message
            const char *err = output ? output : "Command not found\n";
            fprintf(stderr, "[ERROR] [Client #%d - %s:%d] Command not found: \"%s\"\n", client_num, client_ip, client_port, buffer);
            printf("[OUTPUT] [Client #%d - %s:%d] Sending error message to client:\n%s", client_num, client_ip, client_port, err);
            fflush(stdout);
            if (send(client_fd, err, strlen(err), 0) < 0) {
                fprintf(stderr, "[ERROR] [Client #%d - %s:%d] Failed to send error: %s\n", client_num, client_ip, client_port, strerror(errno));
                break;
            }
        } else if (strlen(output) == 0) {
            // No output produced, send a newline to prevent client freeze
            const char *no_output = "\n";
            printf("[OUTPUT] [Client #%d - %s:%d] Sending output to client:\n%s\n", client_num, client_ip, client_port, no_output);
            fflush(stdout);
            if (send(client_fd, no_output, strlen(no_output), 0) < 0) {
                fprintf(stderr, "[ERROR] [Client #%d - %s:%d] Failed to send output: %s\n", client_num, client_ip, client_port, strerror(errno));
                break;
            }
        } else {
            // Command succeeded — forward output to client
            printf("[OUTPUT] [Client #%d - %s:%d] Sending output to client:\n%s\n", client_num, client_ip, client_port, output);
            fflush(stdout);
            if (send(client_fd, output, strlen(output), 0) < 0) {
                fprintf(stderr, "[ERROR] [Client #%d - %s:%d] Failed to send output: %s\n", client_num, client_ip, client_port, strerror(errno));
                break;
            }
        }

        free(output);
        printf("\n");
    }

    // Clean up: close client socket and free memory
    close(client_fd);
    free(client_info);
    pthread_exit(NULL);
    return NULL;
}

int main(void) {
    int server_fd;
    int opt = 1;
    struct sockaddr_in address, client_addr;
    socklen_t addrlen = sizeof(client_addr);

    // Initialize the semaphore for client/thread counter
    if (sem_init(&client_num_sem, 0, 1) != 0) {
        perror("sem_init failed");
        exit(EXIT_FAILURE);
    }

    // Create the TCP socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Allow immediate restart without "Address already in use" errors
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind to socket and start listening
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("[INFO] Server started, waiting for client connections...\n");
    fflush(stdout);

    int thread_counter = 0;
    while (1) {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (client_fd < 0) {
            fprintf(stderr, "[ERROR] accept failed: %s\n", strerror(errno));
            continue;
        }

        // Assign client number and thread number using semaphore for mutual exclusion
        int client_num, thread_num;
        sem_wait(&client_num_sem);
        client_num = ++client_counter;
        thread_num = ++thread_counter;
        sem_post(&client_num_sem);

        // Allocate and fill client info struct for the thread
        client_info_t *client_info = malloc(sizeof(client_info_t));
        if (!client_info) {
            fprintf(stderr, "[ERROR] malloc failed for client_info_t: %s\n", strerror(errno));
            close(client_fd);
            continue;
        }
        client_info->client_fd = client_fd;
        client_info->client_addr = client_addr;
        client_info->client_num = client_num;
        client_info->thread_num = thread_num;

        // Create a new thread to handle the client
        pthread_t thread;
        if (pthread_create(&thread, NULL, ThreadFunction, (void *)client_info) != 0) {
            fprintf(stderr, "[ERROR] pthread_create failed: %s\n", strerror(errno));
            close(client_fd);
            free(client_info);
            continue;
        }
        // Detach thread to reclaim resources automatically when it finishes
        pthread_detach(thread);
    }

    close(server_fd);
    sem_destroy(&client_num_sem);
    return 0;
}
