// scheduler.h — Phase 4 scheduler interface (SRJF + Round-Robin).

#ifndef SCHEDULER_H
#define SCHEDULER_H

#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <sys/types.h>
#include <time.h>

// tuning constants
#define MAX_TASKS     100   // max tasks in the queue at once
#define QUANTUM_FIRST   3   // time-slice for round 1 (seconds)
#define QUANTUM_REST    7   // time-slice for rounds 2+ (seconds)
#define DEFAULT_BURST  10   // burst used for unknown programs
#define SCH_POLL_MS   200   // polling interval inside a slice (ms)
#define BUFFER_SIZE  4096   // max command string length

// task lifecycle states
typedef enum {
    TASK_EMPTY   = 0,   // slot is free
    TASK_WAITING = 1,   // in the ready queue
    TASK_RUNNING = 2,   // currently executing
    TASK_DONE    = 3    // finished; slot will be reclaimed
} TaskState;

// all information the scheduler needs for one client request
typedef struct {
    int        task_id;               // unique 1-based ID; 0 = empty slot
    int        client_num;
    int        client_fd;             // socket fd for sending the response
    char       command[BUFFER_SIZE];

    int        burst_time;            // original burst (-1 for shell commands)
    int        remaining_time;        // decremented each slice
    int        round;                 // starts at 1

    int        is_shell_cmd;          // 1 = shell command, 0 = program
    TaskState  state;
    time_t     arrival_time;          // for FCFS tie-breaking

    pid_t      pid;                   // child PID; -1 if not forked yet
    int        pipe_read;             // read end of the output-capture pipe
    int        cancelled;             // set to 1 when the client disconnects
} Task;

// one entry in the Gantt-chart history linked list
typedef struct HistEntry {
    int              client_num;
    long             end_time;        // seconds since scheduler_init()
    struct HistEntry *next;
} HistEntry;

// shared scheduling state; all fields below the mutex need the mutex held
typedef struct {
    Task            tasks[MAX_TASKS];
    int             count;            // active (waiting or running) task count
    pthread_mutex_t mutex;
    pthread_cond_t  has_task;         // signalled when a task is added

    int             next_task_id;     // monotonically increasing ID counter
    int             last_run_task_id; // ID of most recently run task (-1 = none)
    int             preempt_flag;     // set by client thread to request preemption

    time_t          start_time;       // epoch time at scheduler_init()
    HistEntry      *hist_head;
    HistEntry      *hist_tail;
} TaskQueue;

// initialise the queue; call once from main before spawning any thread
void scheduler_init(TaskQueue *q);

// enqueue a new command; returns the task_id or -1 if queue is full
int scheduler_add_task(TaskQueue *q, int client_num, int client_fd,
                       const char *command, int burst_time, int is_shell_cmd);

// cancel all tasks for a disconnected client
void scheduler_remove_client(TaskQueue *q, int client_num);

// main scheduling loop; runs in its own dedicated thread
void *scheduler_run(void *arg);

// print the Gantt-chart history; called automatically when the queue empties
void scheduler_print_summary(TaskQueue *q);

// destroy mutex/condvar and free history; call only after scheduler thread exits
void scheduler_cleanup(TaskQueue *q);

#endif // SCHEDULER_H
