// client.c
// TCP client for the multithreaded shell server.
// Connects to the server, sends commands entered by the user, and prints responses.
// Typing "exit" sends the command to the server first so it can log the disconnect,
// then waits for the server to close the connection before printing "Disconnected from server."

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

int main(void) {
    int sock;
    struct sockaddr_in serv_addr;
    char send_buf[BUFFER_SIZE];  // Holds the command entered by the user
    char recv_buf[BUFFER_SIZE];  // Holds the response received from the server
    int  bytes;                  // Return value of recv(), reused throughout

    // Create the TCP socket. AF_INET = IPv4, SOCK_STREAM = TCP.
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set up the server address to connect to.
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(PORT);

    // inet_pton() converts the dotted-decimal string to binary form.
    // Returns 1 on success, 0 if the address is invalid, -1 on error.
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("inet_pton failed: invalid server address");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Initiate the TCP connection to the server.
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect failed: server may not be running");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Connected to a server\n");
    fflush(stdout);

    // Prompt loop: runs until "exit", EOF, or a socket error.
    while (1) {
        // Print the prompt and flush so it appears before the user types.
        printf("$ ");
        fflush(stdout);

        // fgets() reads a line from stdin. Returns NULL on EOF (Ctrl-D).
        if (fgets(send_buf, BUFFER_SIZE, stdin) == NULL) {
            printf("\n");
            break;
        }

        // Strip the trailing newline so the server receives a clean string.
        size_t len = strlen(send_buf);
        if (len > 0 && send_buf[len - 1] == '\n')
            send_buf[--len] = '\0';

        // Ignore blank lines and just show the prompt again.
        if (len == 0)
            continue;

        // Ignore ANSI escape sequences from arrow keys since there is no
        // readline support and sending them to the server causes errors.
        if (strchr(send_buf, '\033') != NULL)
            continue;

        // Send the command to the server.
        if (send(sock, send_buf, len, 0) < 0) {
            perror("send failed");
            break;
        }

        // If the command is "exit", wait for the server to close the socket,
        // then print the disconnect message and exit.
        // "exit" was already sent above so the server can log the disconnect.
        if (strcmp(send_buf, "exit") == 0) {
            bytes = recv(sock, recv_buf, BUFFER_SIZE - 1, 0);
            if (bytes < 0)
                perror("recv failed while waiting for server to close");
            printf("Disconnected from server.\n");
            break;
        }

        // Receive the server's response.
        // recv() returns 0 if the server closed the connection, -1 on error.
        bytes = recv(sock, recv_buf, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            if (bytes == 0)
                printf("Server disconnected.\n");
            else
                perror("recv failed");
            break;
        }

        // Null-terminate before printing so printf treats it as a string.
        recv_buf[bytes] = '\0';
        printf("%s", recv_buf);

        // If the output does not end with a newline, add one so the next
        // prompt appears on a clean line.
        if (recv_buf[bytes - 1] != '\n')
            printf("\n");
    }

    close(sock);
    return 0;
}
