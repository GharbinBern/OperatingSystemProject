#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include "shell.h"

#define PORT 8080
#define BUFFER_SIZE 4096

int main() {
    int server_fd, client_fd, opt = 1;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    // Socket setup
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind and listen
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 5) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    printf("[INFO] Server started, waiting for client connections...\n");

    // Accept loop
    if ((client_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen)) < 0) {
        perror("accept failed");
        exit(EXIT_FAILURE);
    }
    printf("[INFO] Client connected.\n");

    // Receive -> execute -> send loop
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                printf("[INFO] Client disconnected.\n");
            } else {
                perror("recv failed");
            }
            break;
        }
        buffer[bytes_read] = '\0';
        // Remove trailing newline if present
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') buffer[len-1] = '\0';

        printf("[RECEIVED] Received command: \"%s\" from client.\n", buffer);
        printf("[EXECUTING] Executing command: \"%s\"\n", buffer);

        char* output = execute_command(buffer);
        if (!output || strstr(output, "Error: Command not found") != NULL) {
            printf("[ERROR] Command not found: \"%s\"\n", buffer);
            printf("[OUTPUT] Sending error message to client: %s\n", output ? output : "Unknown error");
            send(client_fd, output ? output : "Error: Command not found\n", strlen(output ? output : "Error: Command not found\n"), 0);
        } else {
            printf("[OUTPUT] Sending output to client:\n%s\n", output);
            send(client_fd, output, strlen(output), 0);
        }
        free(output);
    }

    close(client_fd);
    close(server_fd);
    return 0;
}
