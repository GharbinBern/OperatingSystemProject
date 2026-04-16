/*
 * client.c
 *
 * TCP client for the multithreaded shell server (Phase 3).
 *
 * Overview:
 *   This program connects to the shell server running on 127.0.0.1:PORT.
 *   It presents an interactive "$" prompt, reads commands from stdin, sends
 *   them to the server, and prints the server's response.
 *
 * Exit behaviour:
 *   Typing "exit" sends the string to the server so the server can log the
 *   graceful disconnect (required by Phase 3 Figure 1).  The client then
 *   waits for the server to close its end of the socket (recv returns 0)
 *   before printing "Disconnected from server." and terminating.  This
 *   matches the expected client output shown in Phase 3 Figure 4.
 *
 * Main logic steps:
 *   1. Create a TCP socket and connect to the server.
 *   2. Enter the prompt loop:
 *        a. Print "$ " and read a line from stdin.
 *        b. If EOF (Ctrl-D), break cleanly.
 *        c. Strip trailing newline; ignore blank lines and ANSI escape sequences.
 *        d. Send the command string to the server.
 *        e. If the command was "exit", wait for the server to close the
 *           connection, print "Disconnected from server.", and exit.
 *        f. Otherwise, receive the server's response and print it.
 *   3. Close the socket and exit.
 *
 * Error handling:
 *   Every socket call is checked.  On failure the program prints the error,
 *   closes the socket, and exits with EXIT_FAILURE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#define PORT        3000   /* Must match the server's PORT constant. */
#define BUFFER_SIZE 4096   /* Size of send and receive buffers.      */

int main(void) {
    int sock;
    struct sockaddr_in serv_addr;
    char send_buf[BUFFER_SIZE]; /* Holds the command typed by the user.   */
    char recv_buf[BUFFER_SIZE]; /* Holds the response received from server. */

    /* ── 1. Create the TCP socket ──────────────────────────────────── */

    /*
     * AF_INET = IPv4, SOCK_STREAM = TCP.
     * socket() returns a file descriptor on success or -1 on error.
     */
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    /* Configure the server address to connect to. */
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(PORT);

    /*
     * inet_pton() converts the dotted-decimal IP string to binary form.
     * Returns 1 on success, 0 if the string is not a valid address, -1 on error.
     */
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("inet_pton failed: invalid server address");
        close(sock);
        exit(EXIT_FAILURE);
    }

    /*
     * Establish the TCP connection to the server.
     * connect() blocks until the connection is established or fails.
     */
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect failed: server may not be running");
        close(sock);
        exit(EXIT_FAILURE);
    }

    /* ── 2. Prompt loop ────────────────────────────────────────────── */

    while (1) {
        /* Display the shell prompt and flush so it appears before input. */
        printf("$ ");
        fflush(stdout);

        /*
         * fgets() reads at most BUFFER_SIZE-1 characters from stdin,
         * including the newline.  Returns NULL on EOF (Ctrl-D).
         */
        if (fgets(send_buf, BUFFER_SIZE, stdin) == NULL) {
            /* EOF: exit cleanly without printing an error message. */
            printf("\n");
            break;
        }

        /* Strip the trailing newline so the server receives a clean string. */
        size_t len = strlen(send_buf);
        if (len > 0 && send_buf[len - 1] == '\n') {
            send_buf[--len] = '\0';
        }

        /* Ignore blank lines — just redisplay the prompt. */
        if (len == 0) {
            continue;
        }

        /*
         * Ignore ANSI/VT escape sequences (arrow keys, function keys, etc.).
         * fgets() captures raw terminal byte sequences because there is no
         * readline/libedit support.  Sending them to the server would produce
         * confusing errors, so we silently discard them.
         */
        if (strchr(send_buf, '\033') != NULL) {
            continue;
        }

        /* ── Send the command to the server ─────────────────────── */

        /*
         * send() transmits the command string (without the newline we stripped).
         * On failure, print the error and abort the loop.
         */
        if (send(sock, send_buf, len, 0) < 0) {
            perror("send failed");
            break;
        }

        /* ── Handle "exit" ─────────────────────────────────────── */

        /*
         * When the user types "exit":
         *   1. We already sent "exit" to the server above so the server can
         *      log "[INFO] Client requested disconnect." (Phase 3 Figure 1).
         *   2. Now wait for the server to close its end of the socket.
         *      recv() will return 0 once the server closes the connection.
         *   3. Print "Disconnected from server." to match Figure 4.
         *   4. Break out of the loop; the socket is closed below.
         */
        if (strcmp(send_buf, "exit") == 0) {
            /* Block until the server closes the connection (recv returns 0). */
            int bytes = recv(sock, recv_buf, BUFFER_SIZE - 1, 0);
            if (bytes < 0) {
                /* A real socket error occurred while waiting. */
                perror("recv failed while waiting for server to close");
            }
            /* Regardless of recv result, the session is ending. */
            printf("Disconnected from server.\n");
            break;
        }

        /* ── Receive and display the server's response ──────────── */

        /*
         * A single recv() call is sufficient for this protocol because each
         * command produces one response that fits within BUFFER_SIZE.
         * recv() returns:
         *   >0  – bytes received (normal)
         *    0  – server closed the connection unexpectedly
         *   <0  – socket error
         */
        int bytes = recv(sock, recv_buf, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            if (bytes == 0) {
                printf("Server disconnected.\n");
            } else {
                perror("recv failed");
            }
            break;
        }

        /* Null-terminate the received data before treating it as a string. */
        recv_buf[bytes] = '\0';
        printf("%s", recv_buf);

        /*
         * If the server's output does not end with a newline, force the next
         * prompt to appear on a fresh line to prevent a mangled display.
         */
        if (recv_buf[bytes - 1] != '\n') {
            printf("\n");
        }
    }

    /* ── 3. Cleanup ─────────────────────────────────────────────────── */
    close(sock);
    return 0;
}
