/*
 * client.c
 *
 * TCP client for the multithreaded shell server (Phase 3).
 *
 * This program connects to the server on 127.0.0.1:PORT, shows a "$" prompt,
 * reads commands from stdin, sends them to the server, and prints the response.
 *
 * When the user types "exit", the client first sends the word "exit" to the
 * server so the server can log the graceful disconnect (as shown in Figure 1).
 * It then waits for the server to close its end of the socket, and prints
 * "Disconnected from server." before exiting. This matches the expected client
 * output in Figure 4.
 *
 * Every socket call is checked for errors. On failure, an error message is
 * printed and the client exits cleanly after closing the socket.
 *
 * Main steps:
 *   1. Create a TCP socket and connect to the server.
 *   2. Enter the prompt loop:
 *        a. Print "$ " and read a line from stdin.
 *        b. On EOF (Ctrl-D), break cleanly.
 *        c. Strip the trailing newline; skip blank lines and escape sequences.
 *        d. Send the command to the server.
 *        e. If the command was "exit", wait for the server to close the
 *           connection, print "Disconnected from server.", then exit.
 *        f. Otherwise, receive and print the server response.
 *   3. Close the socket and exit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#define PORT        3000   /* Must match the PORT value in server.c */
#define BUFFER_SIZE 4096   /* Size of the send and receive buffers  */

int main(void) {
    int sock;
    struct sockaddr_in serv_addr;
    char send_buf[BUFFER_SIZE]; /* Holds the command entered by the user */
    char recv_buf[BUFFER_SIZE]; /* Holds the response received from the server */
    int  bytes;                 /* Reused for recv() return value throughout */

    /* Create a TCP socket. AF_INET = IPv4, SOCK_STREAM = TCP. */
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    /* Set up the server address structure for the connection. */
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(PORT);

    /*
     * inet_pton() converts the dotted-decimal address string "127.0.0.1"
     * into the binary form expected by the socket API. Returns 1 on success,
     * 0 if the string is invalid, or -1 on a system error.
     */
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("inet_pton failed: invalid server address");
        close(sock);
        exit(EXIT_FAILURE);
    }

    /* connect() initiates the TCP handshake with the server. */
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect failed: server may not be running");
        close(sock);
        exit(EXIT_FAILURE);
    }

    /* Prompt loop: runs until "exit", EOF, or a socket error. */
    while (1) {
        /* Print the prompt and flush immediately so it appears before input. */
        printf("$ ");
        fflush(stdout);

        /*
         * fgets() reads up to BUFFER_SIZE-1 characters from stdin, including
         * the newline. It returns NULL when EOF is reached (Ctrl-D).
         */
        if (fgets(send_buf, BUFFER_SIZE, stdin) == NULL) {
            printf("\n");
            break;
        }

        /* Remove the trailing newline so the server gets a clean command string. */
        size_t len = strlen(send_buf);
        if (len > 0 && send_buf[len - 1] == '\n') {
            send_buf[--len] = '\0';
        }

        /* Ignore blank lines and just redisplay the prompt. */
        if (len == 0) {
            continue;
        }

        /*
         * Skip ANSI/VT escape sequences produced by arrow keys and function
         * keys. Because there is no readline support, fgets() captures these
         * as raw bytes. Sending them to the server would cause confusing errors.
         */
        if (strchr(send_buf, '\033') != NULL) {
            continue;
        }

        /* Send the command to the server. */
        if (send(sock, send_buf, len, 0) < 0) {
            perror("send failed");
            break;
        }

        /*
         * If the command is "exit", the string has already been sent above
         * so the server can log the graceful disconnect. Now wait for the
         * server to close its end of the socket (recv returns 0 on EOF),
         * then print the disconnect message and exit the loop.
         */
        if (strcmp(send_buf, "exit") == 0) {
            bytes = recv(sock, recv_buf, BUFFER_SIZE - 1, 0);
            if (bytes < 0) {
                perror("recv failed while waiting for server to close");
            }
            printf("Disconnected from server.\n");
            break;
        }

        /*
         * For all other commands, receive the server response and print it.
         * recv() returns the number of bytes received, 0 if the server closed
         * the connection, or -1 on a socket error.
         */
        bytes = recv(sock, recv_buf, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            if (bytes == 0) {
                printf("Server disconnected.\n");
            } else {
                perror("recv failed");
            }
            break;
        }

        /* Null-terminate before printing so printf treats it as a string. */
        recv_buf[bytes] = '\0';
        printf("%s", recv_buf);

        /*
         * If the server output did not end with a newline, force one so the
         * next prompt appears on a clean line instead of running together
         * with the output.
         */
        if (recv_buf[bytes - 1] != '\n') {
            printf("\n");
        }
    }

    close(sock);
    return 0;
}
