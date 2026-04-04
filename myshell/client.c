
/*
 * This program implements a TCP client that connects to the shell server on 127.0.0.1:PORT.
 * It presents a "$" prompt to the user, reads commands from stdin, sends them to the server,
 * and prints the server's response. Typing "exit" closes the socket and exits cleanly.
 *
 * Main logic steps:
 *   1. Create a TCP socket and connect to the server.
 *   2. Enter a prompt loop:
 *      - Print "$" prompt and read user input.
 *      - If input is "exit", close the socket and exit.
 *      - Ignore blank lines.
 *      - Send the command to the server.
 *      - Receive and print the server's response.
 *      - Ensure prompt appears on a new line even if output lacks a newline.
 *   3. Clean up and exit on EOF or server disconnect.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT        8080
#define BUFFER_SIZE 4096

int main(void) {
    int sock;
    struct sockaddr_in serv_addr;
    char send_buf[BUFFER_SIZE];
    char recv_buf[BUFFER_SIZE];

    // 1: Create socket and connect to server
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("inet_pton failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // 2: Prompt loop 
    while (1) {
        printf("$ ");
        fflush(stdout);

        if (fgets(send_buf, BUFFER_SIZE, stdin) == NULL) {
            // EOF (Ctrl-D): exit gracefully
            printf("\n");
            break;
        }

        // Strip trailing newline
        size_t len = strlen(send_buf);
        if (len > 0 && send_buf[len - 1] == '\n') {
            send_buf[--len] = '\0';
        }

        // Handle "exit", close socket cleanly 
        if (strcmp(send_buf, "exit") == 0) {
            break;
        }

        // Ignore blank lines
        if (len == 0) {
            continue;
        }

        // Send command to server
        if (send(sock, send_buf, len, 0) < 0) {
            perror("send failed");
            break;
        }

        // 3: Receive and display the server's response 
        int bytes = recv(sock, recv_buf, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            if (bytes == 0) {
                printf("Server disconnected.\n");
            } else {
                perror("recv failed");
            }
            break;
        }

        recv_buf[bytes] = '\0';
        printf("%s", recv_buf);

        // Ensure the prompt appears on a fresh line even if output lacks '\n'
        if (recv_buf[bytes - 1] != '\n') {
            printf("\n");
        }
    }

    close(sock);
    return 0;
}
