/*
 * server.c — Phase 2 Socket Shell Server
 *
 * Listens on PORT for a single client connection. For each command
 * received it calls execute_command() (shell.c), logs the interaction
 * using the required [INFO/RECEIVED/EXECUTING/ERROR/OUTPUT] tags, and
 * sends the output back to the client.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include "shell.h"

#define PORT        8080
#define BUFFER_SIZE 4096

int main(void) {
    int server_fd, client_fd;
    int opt = 1;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    /* --- Step 1: Create the TCP socket --- */
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    /* Allow immediate restart without "Address already in use" errors */
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* --- Step 2: Bind to INADDR_ANY:PORT and start listening --- */
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

    /* --- Step 3: Accept one client connection --- */
    if ((client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
        perror("accept failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("[INFO] Client connected.\n");
    fflush(stdout);

    /* --- Step 4: Receive → execute → send loop --- */
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

        /* Step 5: Handle client disconnect or recv error */
        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                printf("[INFO] Client disconnected.\n");
            } else {
                perror("recv failed");
            }
            break;
        }

        buffer[bytes_read] = '\0';

        /* Strip trailing newline sent by the client */
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[--len] = '\0';
        }

        printf("[RECEIVED] Received command: \"%s\" from client.\n", buffer);
        printf("[EXECUTING] Executing command: \"%s\"\n", buffer);
        fflush(stdout);

        /* Run the command through the Phase 1 parser/executor */
        char *output = execute_command(buffer);

        if (!output || strstr(output, "Error:") != NULL) {
            /* Command failed or produced an error message */
            const char *err = output ? output : "Error: Command not found\n";
            printf("[ERROR] Command not found: \"%s\"\n", buffer);
            printf("[OUTPUT] Sending error message to client: %s", err);
            fflush(stdout);
            send(client_fd, err, strlen(err), 0);
        } else {
            /* Command succeeded — forward output to client */
            printf("[OUTPUT] Sending output to client:\n%s\n", output);
            fflush(stdout);
            send(client_fd, output, strlen(output), 0);
        }

        free(output);
    }

    close(client_fd);
    close(server_fd);
    return 0;
}
