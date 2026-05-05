// scheduler.c — Phase 4 SRJF + Round-Robin scheduler implementation.
//
// Shell commands (burst_time = -1) always run first and atomically.
// Programs are scheduled by Shortest-Remaining-Job-First with FCFS tie-breaking.
// Each program slice uses QUANTUM_FIRST (round 1) or QUANTUM_REST (rounds 2+).
// A new program that is shorter than the running one triggers preemption via SIGSTOP.

#define _POSIX_C_SOURCE 200809L

#include "scheduler.h"
#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

// forward declarations for internal helpers
static int  select_next_task(TaskQueue *q);
static void record_history(TaskQueue *q, int client_num);
static void run_shell_task(TaskQueue *q, int idx);
static int  run_program_slice(TaskQueue *q, int idx);
static int  fork_program(Task *t);
static void send_program_output(int client_num, int client_fd, int pipe_read);


// Zero-initialises every slot, sets up the mutex and condvar, records start time.
// Must be called once from main() before any threads start.
void scheduler_init(TaskQueue *q) {
    memset(q, 0, sizeof(TaskQueue));   // task_id == 0 marks every slot as free
    q->next_task_id     = 1;           // IDs are 1-based; 0 means empty
    q->last_run_task_id = -1;          // no task has run yet
    q->preempt_flag     = 0;
    q->start_time       = time(NULL);  // used for relative Gantt timestamps
    q->hist_head        = NULL;
    q->hist_tail        = NULL;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->has_task, NULL);
}


// Called by a client thread to enqueue a new command.
// Finds a free slot, fills the Task descriptor, increments count, signals the
// scheduler thread. Sets preempt_flag if the new program is shorter than the
// currently running one. Returns the assigned task_id, or -1 if the queue is full.
int scheduler_add_task(TaskQueue *q, int client_num, int client_fd,
                       const char *command, int burst_time, int is_shell_cmd) {
    pthread_mutex_lock(&q->mutex);

    // scan for a free slot (task_id == 0 means the slot is available)
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (q->tasks[i].task_id == 0) { slot = i; break; }
    }
    if (slot == -1) {
        fprintf(stderr, "[SCHEDULER] Queue full — dropping command from client %d\n", client_num);
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    // populate the new task descriptor
    Task *t = &q->tasks[slot];
    memset(t, 0, sizeof(Task));
    t->task_id        = q->next_task_id++;
    t->client_num     = client_num;
    t->client_fd      = client_fd;
    strncpy(t->command, command, BUFFER_SIZE - 1);
    t->burst_time     = burst_time;
    t->remaining_time = burst_time;  // remaining_time is decremented each slice
    t->round          = 1;           // first slice is always round 1
    t->is_shell_cmd   = is_shell_cmd;
    t->state          = TASK_WAITING;
    t->arrival_time   = time(NULL);  // used for FCFS tie-breaking
    t->pid            = -1;          // no child forked yet
    t->pipe_read      = -1;          // no pipe open yet
    t->cancelled      = 0;
    q->count++;

    printf("[%d]--- created (%d)\n", client_num, burst_time);
    fflush(stdout);

    // preemption check: if a shorter program arrives while another is running,
    // set the flag so the scheduler stops the current child at its next poll
    if (!is_shell_cmd) {
        for (int i = 0; i < MAX_TASKS; i++) {
            if (q->tasks[i].state == TASK_RUNNING && !q->tasks[i].is_shell_cmd) {
                Task *running = &q->tasks[i];
                if (burst_time < running->remaining_time)
                    q->preempt_flag = 1;  // shorter job arrived; request preemption
                break;
            }
        }
    }

    pthread_cond_signal(&q->has_task);  // wake the scheduler thread if it is waiting
    pthread_mutex_unlock(&q->mutex);
    return t->task_id;
}


// Called when a client disconnects.
// Waiting tasks are dropped immediately; running tasks are killed via SIGKILL.
// The scheduler thread sees cancelled == 1 and skips sending output on the closed fd.
void scheduler_remove_client(TaskQueue *q, int client_num) {
    pthread_mutex_lock(&q->mutex);

    for (int i = 0; i < MAX_TASKS; i++) {
        Task *t = &q->tasks[i];
        if (t->task_id == 0 || t->client_num != client_num) continue;

        if (t->state == TASK_WAITING) {
            t->task_id = 0;  // mark slot free; no child process exists yet
            q->count--;
        } else if (t->state == TASK_RUNNING) {
            t->cancelled = 1;                        // tell scheduler thread to skip output
            if (t->pid > 0) kill(t->pid, SIGKILL);  // kill the child immediately
        }
    }

    pthread_mutex_unlock(&q->mutex);
}


// Prints the Gantt-chart scheduling history to stdout.
// Format: 0)-P<client>-(<end_time>)-P<client>-(<end_time>)...
// Called automatically whenever the queue drains to zero active tasks.
void scheduler_print_summary(TaskQueue *q) {
    pthread_mutex_lock(&q->mutex);
    printf("0)");
    for (HistEntry *e = q->hist_head; e != NULL; e = e->next)
        printf("-P%d-(%ld)", e->client_num, e->end_time);
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&q->mutex);
}


// Destroys synchronisation objects and frees the history linked list.
// Call only after the scheduler thread has exited.
void scheduler_cleanup(TaskQueue *q) {
    pthread_mutex_lock(&q->mutex);

    // kill any surviving child processes and close their pipes
    for (int i = 0; i < MAX_TASKS; i++) {
        Task *t = &q->tasks[i];
        if (t->pid > 0) { kill(t->pid, SIGKILL); waitpid(t->pid, NULL, 0); }
        if (t->pipe_read >= 0) { close(t->pipe_read); t->pipe_read = -1; }
    }

    // free the Gantt history linked list
    HistEntry *e = q->hist_head;
    while (e) { HistEntry *next = e->next; free(e); e = next; }
    q->hist_head = q->hist_tail = NULL;

    pthread_mutex_unlock(&q->mutex);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->has_task);
}


// Main scheduling loop — runs in its own dedicated thread (the single simulated CPU).
// Waits for tasks to arrive, picks the best one, executes it for one slice,
// then either requeues it (program, not done) or reclaims its slot (done/shell).
void *scheduler_run(void *arg) {
    TaskQueue *q = (TaskQueue *)arg;

    printf("[SCHEDULER] Scheduler thread started\n");
    fflush(stdout);

    while (1) {
        pthread_mutex_lock(&q->mutex);

        // block until at least one task is in the queue
        while (q->count == 0)
            pthread_cond_wait(&q->has_task, &q->mutex);

        // pick the best waiting task (SRJF + FCFS + no-consecutive rule)
        int idx = select_next_task(q);
        if (idx == -1) {
            // count > 0 but no WAITING task found — defensive guard against races
            pthread_mutex_unlock(&q->mutex);
            continue;
        }

        Task *t  = &q->tasks[idx];
        t->state = TASK_RUNNING;
        q->preempt_flag = 0;  // clear any stale preemption request before running

        pthread_mutex_unlock(&q->mutex);

        if (t->is_shell_cmd) {
            // shell commands run atomically in one shot; they are never requeued
            run_shell_task(q, idx);

            pthread_mutex_lock(&q->mutex);
            record_history(q, t->client_num);  // log this slice in the Gantt history
            q->last_run_task_id = t->task_id;
            t->task_id = 0;  // reclaim the slot
            q->count--;
            int empty = (q->count == 0);
            pthread_mutex_unlock(&q->mutex);

            if (empty) scheduler_print_summary(q);  // print summary when queue drains

        } else {
            // program tasks run for one quantum then may be requeued
            int completed = run_program_slice(q, idx);

            pthread_mutex_lock(&q->mutex);
            record_history(q, t->client_num);
            q->last_run_task_id = t->task_id;
            q->preempt_flag     = 0;

            if (t->cancelled) {
                // client disconnected mid-run; discard without sending output
                if (t->pid > 0)        { waitpid(t->pid, NULL, WNOHANG); t->pid = -1; }
                if (t->pipe_read >= 0) { close(t->pipe_read); t->pipe_read = -1; }
                t->task_id = 0;
                q->count--;

            } else if (completed) {
                // task finished: send its accumulated output then reclaim slot
                printf("[%d]--- ended (0)\n", t->client_num);
                fflush(stdout);
                // copy fields we need before releasing the mutex (t may be reused after unlock)
                int cfd  = t->client_fd;
                int cnum = t->client_num;
                int pfd  = t->pipe_read;
                t->pipe_read = -1;
                t->task_id   = 0;
                q->count--;
                int empty = (q->count == 0);
                pthread_mutex_unlock(&q->mutex);  // unlock before doing I/O

                send_program_output(cnum, cfd, pfd);
                if (empty) scheduler_print_summary(q);
                continue;  // already unlocked above; skip the unlock at the bottom

            } else {
                // quantum expired or preempted: put the task back in the ready queue
                printf("[%d]--- waiting (%d)\n", t->client_num, t->remaining_time);
                fflush(stdout);
                t->state = TASK_WAITING;
                t->round++;  // increment round so the next slice uses QUANTUM_REST
            }

            int empty = (q->count == 0);
            pthread_mutex_unlock(&q->mutex);
            if (empty) scheduler_print_summary(q);
        }
    }

    return NULL;
}


// Picks the best WAITING task using SRJF with FCFS tie-breaking.
// Skips the last-run task unless it is the only one available (avoids starvation
// of other clients by running the same task twice in a row).
// Must be called with q->mutex held. Returns slot index, or -1 if none found.
static int select_next_task(TaskQueue *q) {
    int    best_idx       = -1;
    int    best_remaining = INT_MAX;  // lower remaining_time wins (SRJF)
    time_t best_arrival   = 0;       // earlier arrival wins ties (FCFS)

    // first pass: prefer any task other than the one that just ran
    for (int i = 0; i < MAX_TASKS; i++) {
        Task *t = &q->tasks[i];
        if (t->task_id == 0 || t->state != TASK_WAITING) continue;
        if (t->task_id == q->last_run_task_id && q->count > 1) continue;  // skip last-run if alternatives exist

        if (t->remaining_time < best_remaining ||
            (t->remaining_time == best_remaining && t->arrival_time < best_arrival)) {
            best_remaining = t->remaining_time;
            best_arrival   = t->arrival_time;
            best_idx       = i;
        }
    }

    // second pass: if every waiting task was the last-run one, select it anyway
    if (best_idx == -1) {
        for (int i = 0; i < MAX_TASKS; i++) {
            if (q->tasks[i].task_id != 0 && q->tasks[i].state == TASK_WAITING) {
                best_idx = i;
                break;
            }
        }
    }

    return best_idx;
}


// Appends one entry to the Gantt-chart history linked list.
// end_time is wall-clock seconds elapsed since scheduler_init().
// Must be called with q->mutex held.
static void record_history(TaskQueue *q, int client_num) {
    HistEntry *e = malloc(sizeof(HistEntry));
    if (!e) return;
    e->client_num = client_num;
    e->end_time   = (long)(time(NULL) - q->start_time);  // relative timestamp
    e->next       = NULL;
    if (q->hist_tail) q->hist_tail->next = e;  // append to tail
    else              q->hist_head       = e;   // first entry
    q->hist_tail = e;
}


// Runs a shell command synchronously using execute_command() and sends the
// output directly to the client socket. Never preempted; runs to completion.
// Called without q->mutex held.
static void run_shell_task(TaskQueue *q, int idx) {
    Task *t = &q->tasks[idx];
    (void)q;  // q not used directly; avoid compiler warning

    printf("[%d]--- started (-1)\n", t->client_num);
    fflush(stdout);

    char *output = execute_command(t->command);  // run the command, capture its output

    if (output == NULL || strstr(output, "Error:") != NULL) {
        // command failed or was not found: send the error string to the client
        const char *err = (output != NULL) ? output : "Error: Command not found\n";
        send(t->client_fd, err, strlen(err), 0);
    } else if (strlen(output) == 0) {
        // command succeeded but produced no output (e.g., mkdir): send a blank line
        send(t->client_fd, "\n", 1, 0);
    } else {
        ssize_t n = send(t->client_fd, output, strlen(output), 0);
        printf("[%d]<<< %zd bytes sent\n", t->client_num, n);
        fflush(stdout);
    }

    free(output);
    printf("[%d]--- ended (-1)\n", t->client_num);
    fflush(stdout);
}


// Forks the child process to execute t->command with stdout and stderr
// redirected into a new pipe. The read end is saved in t->pipe_read and
// survives across SIGSTOP/SIGCONT cycles so output accumulates until the child exits.
// Returns 0 on success, -1 on error.
static int fork_program(Task *t) {
    int pipefd[2];
    if (pipe(pipefd) < 0) { perror("[SCHEDULER] pipe"); return -1; }

    pid_t pid = fork();
    if (pid < 0) {
        perror("[SCHEDULER] fork");
        close(pipefd[0]); close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        // child: redirect both stdout and stderr into the write end of the pipe
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(1);
        if (dup2(pipefd[1], STDERR_FILENO) < 0) _exit(1);
        close(pipefd[1]);

        // tokenize the command string into argv for execvp
        char  cmd_copy[BUFFER_SIZE];
        strncpy(cmd_copy, t->command, BUFFER_SIZE - 1);
        cmd_copy[BUFFER_SIZE - 1] = '\0';

        char *argv[64];
        int   argc = 0;
        char *tok  = strtok(cmd_copy, " \t");
        while (tok && argc < 63) { argv[argc++] = tok; tok = strtok(NULL, " \t"); }
        argv[argc] = NULL;

        if (argc == 0) _exit(1);
        execvp(argv[0], argv);
        // execvp only returns on failure
        fprintf(stderr, "Error: execvp: %s: %s\n", argv[0], strerror(errno));
        _exit(1);
    }

    // parent: close the write end; child holds the only remaining write reference
    close(pipefd[1]);
    t->pipe_read = pipefd[0];  // save read end; stays open through SIGSTOP/SIGCONT
    t->pid       = pid;
    return 0;
}


// Runs (or resumes) the program task at q->tasks[idx] for one quantum slice.
// First call (pid == -1): forks the child. Subsequent calls: sends SIGCONT.
// Polls every SCH_POLL_MS ms for: (a) child exit, (b) preempt_flag set, (c) quantum end.
// Sends SIGSTOP on (b) or (c). Decrements remaining_time by actual elapsed seconds.
// Returns 1 if the task completed this slice, 0 if it was stopped or preempted.
static int run_program_slice(TaskQueue *q, int idx) {
    Task *t = &q->tasks[idx];
    int quantum = (t->round == 1) ? QUANTUM_FIRST : QUANTUM_REST;  // round 1 uses shorter quantum

    if (t->pid == -1) {
        // first time this task runs: fork a child process
        if (fork_program(t) < 0) return 0;
        printf("[%d]--- started (%d)\n", t->client_num, t->remaining_time);
    } else {
        // task was stopped before; resume the child with SIGCONT
        printf("[%d]--- running (%d)\n", t->client_num, t->remaining_time);
        kill(t->pid, SIGCONT);
    }
    fflush(stdout);

    time_t slice_start = time(NULL);
    int    elapsed     = 0;
    int    completed   = 0;

    // polling loop: wake every SCH_POLL_MS ms and check for exit or preemption
    while (elapsed < quantum) {
        struct timespec ts = { 0, (long)SCH_POLL_MS * 1000000L };
        nanosleep(&ts, NULL);
        elapsed = (int)(time(NULL) - slice_start);

        // check whether the child has already exited
        int   status;
        pid_t r = waitpid(t->pid, &status, WNOHANG);
        if (r == t->pid) { completed = 1; t->pid = -1; break; }

        // check whether a client thread requested preemption
        pthread_mutex_lock(&q->mutex);
        int preempt = q->preempt_flag;
        pthread_mutex_unlock(&q->mutex);
        if (preempt) { kill(t->pid, SIGSTOP); break; }  // stop child; scheduler will reschedule
    }

    // edge case: child may have exited exactly when the quantum expired,
    // so the loop exited before the waitpid check ran again
    if (!completed && t->pid > 0) {
        int final_status;
        if (waitpid(t->pid, &final_status, WNOHANG) == t->pid) {
            completed = 1; t->pid = -1;
        }
    }

    // quantum expired without completion or preemption: stop the child now
    if (!completed && t->pid > 0) {
        pthread_mutex_lock(&q->mutex);
        int preempt = q->preempt_flag;
        pthread_mutex_unlock(&q->mutex);
        if (!preempt) kill(t->pid, SIGSTOP);  // preempt path already stopped it in the loop
    }

    // update remaining time by actual seconds used this slice
    t->remaining_time -= elapsed;
    if (t->remaining_time < 0) t->remaining_time = 0;

    return completed;
}


// Reads all accumulated output from the child's pipe and forwards it to the
// client socket. Closes the pipe when done.
// Called without q->mutex held to avoid holding the lock during blocking I/O.
static void send_program_output(int client_num, int client_fd, int pipe_read) {
    char    buf[65536];  // large buffer; programs are expected to produce modest output
    int     total = 0;
    ssize_t n;

    // read until EOF (child exited, so the write end of the pipe is closed)
    while (total < (int)(sizeof(buf) - 1) &&
           (n = read(pipe_read, buf + total, sizeof(buf) - (size_t)total - 1)) > 0)
        total += (int)n;

    buf[total] = '\0';
    close(pipe_read);  // done reading; release the fd

    if (total > 0) {
        ssize_t sent = send(client_fd, buf, (size_t)total, 0);
        printf("[%d]<<< %zd bytes sent\n", client_num, sent);
    } else {
        send(client_fd, "\n", 1, 0);  // send a blank line so the client isn't left waiting
    }
    fflush(stdout);
}
