// server.c — Phase 4 multithreaded TCP shell server.
//
// Thread model:
//   main thread      — accepts TCP connections, spawns one client thread each.
//   scheduler thread — runs scheduler_run(); the single simulated CPU.
//   client threads   — one per client; receives commands and enqueues them.

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
#include <fcntl.h>

#include "shell.h"
#include "scheduler.h"

#define PORT        3000   // TCP port the server listens on
#define BUFFER_SIZE 4096   // max length of one incoming command

// global scheduler queue shared by all threads
static TaskQueue   g_queue;

// monotonic client counter; protected by client_sem so accepts never get duplicate IDs
static int         client_counter  = 0;
static sem_t      *client_sem      = NULL;
static const char *CLIENT_SEM_NAME = "/client_sem";

// per-connection data passed to each client thread; heap-allocated, freed by the thread
typedef struct {
    int                client_fd;
    struct sockaddr_in client_addr;
    int                client_num;
    int                thread_num;
} client_info_t;


// Classifies a command as a program or a shell command and sets burst_time.
// Commands starting with "./" are programs; everything else is a shell command.
// For "./demo N", burst_time is parsed from N. Other "./" programs use DEFAULT_BURST.
// Shell commands get burst_time = -1 (they are always scheduled first and run atomically).
static void classify_command(const char *command, int *burst_out, int *is_shell_out) {
    if (strncmp(command, "./", 2) == 0) {
        *is_shell_out = 0;
        *burst_out    = DEFAULT_BURST;  // fallback for programs with unknown burst
        const char *last_space = strrchr(command, ' ');
        if (last_space != NULL && *(last_space + 1) != '\0') {
            int n = atoi(last_space + 1);
            if (n > 0) *burst_out = n;  // override with the N parsed from the command
        }
    } else {
        *is_shell_out = 1;
        *burst_out    = -1;  // -1 marks shell commands so the scheduler runs them first
    }
}


// Entry point for each per-client thread.
// Loops reading commands, classifies each one, and enqueues it with the scheduler.
// The scheduler thread handles all execution and sends responses back on client_fd.
// Exits when the client sends "exit" or disconnects.
static void *ThreadFunction(void *arg) {
    client_info_t *info       = (client_info_t *)arg;
    int            client_fd  = info->client_fd;
    int            client_num = info->client_num;

    printf("[%d]<<< client connected\n", client_num);
    fflush(stdout);

    char    buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);

        // block until the client sends data or disconnects
        bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_read <= 0) {
            // 0 = clean disconnect; negative = error
            if (bytes_read == 0)
                printf("[%d] disconnected.\n", client_num);
            else
                fprintf(stderr, "[ERROR] recv client %d: %s\n", client_num, strerror(errno));
            break;
        }

        buffer[bytes_read] = '\0';

        // strip the trailing newline that client.c's fgets() adds
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n')
            buffer[--len] = '\0';

        printf("[%d]>>> %s\n", client_num, buffer);  // log the received command
        fflush(stdout);

        // "exit" closes the connection; client is waiting for the socket to close
        if (strcmp(buffer, "exit") == 0)
            break;

        // determine burst_time and type, then add to the scheduler queue
        int burst_time, is_shell_cmd;
        classify_command(buffer, &burst_time, &is_shell_cmd);

        int task_id = scheduler_add_task(&g_queue, client_num, client_fd,
                                         buffer, burst_time, is_shell_cmd);
        if (task_id < 0) {
            // queue full: send error immediately so the client isn't left hanging
            const char *err = "Error: Server task queue is full. Try again later.\n";
            send(client_fd, err, strlen(err), 0);
        }
        // the client thread does NOT wait for the result here;
        // client.c is synchronous so recv() above naturally blocks until
        // the scheduler sends the response on client_fd
    }

    // cancel any queued/running tasks before closing the socket so the
    // scheduler never tries to send on a closed file descriptor
    scheduler_remove_client(&g_queue, client_num);

    close(client_fd);
    free(info);           // info was malloc'd in the accept loop
    pthread_exit(NULL);
    return NULL;
}


int main(void) {
    // create a named semaphore (value=1) to protect client_counter
    sem_unlink(CLIENT_SEM_NAME);  // remove stale instance from a previous run
    client_sem = sem_open(CLIENT_SEM_NAME, O_CREAT | O_EXCL, 0600, 1);
    if (client_sem == SEM_FAILED) { perror("sem_open"); exit(EXIT_FAILURE); }

    // create the TCP server socket
    int server_fd, opt = 1;
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket"); exit(EXIT_FAILURE);
    }

    // SO_REUSEADDR lets the server rebind immediately after a restart
    // without waiting for the port to leave the TIME_WAIT state
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt"); close(server_fd); exit(EXIT_FAILURE);
    }

    // bind to all interfaces on PORT
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind"); close(server_fd); exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 5) < 0) {
        perror("listen"); close(server_fd); exit(EXIT_FAILURE);
    }

    // initialise the shared task queue and spawn the dedicated scheduler thread
    scheduler_init(&g_queue);

    pthread_t sched_thread;
    if (pthread_create(&sched_thread, NULL, scheduler_run, &g_queue) != 0) {
        perror("pthread_create scheduler"); close(server_fd); exit(EXIT_FAILURE);
    }
    pthread_detach(sched_thread);  // scheduler runs forever; no need to join it

    printf("| Hello, Server Started |\n");
    printf("----------------------------\n");
    fflush(stdout);

    int thread_counter = 0;  // used only to assign thread_num (for info struct)

    // accept loop: one iteration per incoming client connection
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addrlen   = sizeof(client_addr);
        int       client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (client_fd < 0) {
            // accept() can fail transiently (e.g., interrupted by a signal)
            fprintf(stderr, "[ERROR] accept: %s\n", strerror(errno));
            continue;
        }

        // assign unique client and thread numbers atomically under the semaphore
        int client_num, thread_num;
        sem_wait(client_sem);
        client_num = ++client_counter;
        thread_num = ++thread_counter;
        sem_post(client_sem);

        // allocate the per-client struct; the client thread frees it on exit
        client_info_t *info = malloc(sizeof(client_info_t));
        if (!info) {
            fprintf(stderr, "[ERROR] malloc: %s\n", strerror(errno));
            close(client_fd);
            continue;
        }
        info->client_fd   = client_fd;
        info->client_addr = client_addr;
        info->client_num  = client_num;
        info->thread_num  = thread_num;

        // spawn a thread for this client; detach so it cleans up automatically on exit
        pthread_t thread;
        if (pthread_create(&thread, NULL, ThreadFunction, info) != 0) {
            fprintf(stderr, "[ERROR] pthread_create: %s\n", strerror(errno));
            close(client_fd);
            free(info);
            continue;
        }
        pthread_detach(thread);
    }

    // unreachable during normal operation; here for completeness
    close(server_fd);
    sem_close(client_sem);
    sem_unlink(CLIENT_SEM_NAME);
    scheduler_cleanup(&g_queue);
    return 0;
}
