// client.c
// TCP client for the multithreaded shell server.
// Connects to the server, sends commands entered by the user, and prints responses.
//
// The server terminates every command's output with an EOT byte (0x04).
// The client loops on recv() until it receives that marker so that iterative
// program output (one line per second from demo.c) is displayed as it arrives
// rather than all at once at the end.
//
// Typing "exit" sends the command to the server first so it can log the disconnect,
// then waits for the server to close the connection before printing "Disconnected."

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#define PORT        3000   // Must match the PORT value in server.c
#define BUFFER_SIZE 4096   // Size of the send and receive buffers
#define EOT         '\x04' // End-of-response marker sent by server after each command

int main(void) {
    int sock;
    struct sockaddr_in serv_addr;
    char send_buf[BUFFER_SIZE];
    char recv_buf[BUFFER_SIZE];
    int  bytes;

    // Create the TCP socket.
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("inet_pton failed: invalid server address");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect failed: server may not be running");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Connected to a server\n");
    fflush(stdout);

    while (1) {
        printf("$ ");
        fflush(stdout);

        if (fgets(send_buf, BUFFER_SIZE, stdin) == NULL) {
            printf("\n");
            break;
        }

        // Strip trailing newline.
        size_t len = strlen(send_buf);
        if (len > 0 && send_buf[len - 1] == '\n')
            send_buf[--len] = '\0';

        if (len == 0)
            continue;

        // Ignore ANSI escape sequences from arrow keys.
        if (strchr(send_buf, '\033') != NULL)
            continue;

        if (send(sock, send_buf, len, 0) < 0) {
            perror("send failed");
            break;
        }

        // "exit": send to server for logging, then wait for it to close the socket.
        if (strcmp(send_buf, "exit") == 0) {
            bytes = recv(sock, recv_buf, BUFFER_SIZE - 1, 0);
            if (bytes < 0)
                perror("recv failed while waiting for server to close");
            printf("Disconnected from server.\n");
            break;
        }

        // Receive the server's response, looping until the EOT marker (0x04) arrives.
        // The server sends EOT after every command's complete output, whether the command
        // ran atomically (shell) or iteratively (program like demo).
        int server_closed = 0;
        while (1) {
            bytes = recv(sock, recv_buf, BUFFER_SIZE - 1, 0);
            if (bytes <= 0) {
                if (bytes == 0)
                    printf("Server disconnected.\n");
                else
                    perror("recv failed");
                server_closed = 1;
                break;
            }

            // Scan for the EOT marker.
            int eot = -1;
            for (int i = 0; i < bytes; i++) {
                if ((unsigned char)recv_buf[i] == (unsigned char)EOT) {
                    eot = i;
                    break;
                }
            }

            if (eot >= 0) {
                // Print everything before the EOT marker, then stop.
                if (eot > 0) {
                    recv_buf[eot] = '\0';
                    printf("%s", recv_buf);
                    if (recv_buf[eot - 1] != '\n')
                        printf("\n");
                }
                break;
            } else {
                // Intermediate chunk: print and keep receiving.
                recv_buf[bytes] = '\0';
                printf("%s", recv_buf);
                fflush(stdout);
            }
        }

        if (server_closed)
            break;
    }

    close(sock);
    return 0;
}
