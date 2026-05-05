# Makefile — Phase 4 Multithreaded Shell Server with Scheduling
#
# Targets:
#   all     – build myshell, server, client, and demo
#   myshell – Phase 1 interactive shell (cumulative requirement)
#   server  – Phase 4 server with SRJF + RR scheduler
#   client  – TCP client
#   demo    – demo program used for scheduler testing (./demo N)
#   clean   – remove all object files and binaries

CC     = gcc

# -Wall/-Wextra:            enable most useful warnings
# -std=c99:                 C99 standard
# -D_POSIX_C_SOURCE=200809L: expose POSIX.1-2008 APIs on Linux
# -g:                       include debug symbols
CFLAGS = -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L -g

# ── Phase 1: interactive shell ────────────────────────────────────────────
SHELL_SRCS = main.c input.c parse.c execute.c
SHELL_OBJS = $(SHELL_SRCS:.c=.o)
SHELL_BIN  = myshell

# ── Phase 4: server (scheduler.c added; still needs -lpthread) ────────────
SERVER_SRCS = server.c scheduler.c shell.c parse.c execute.c
SERVER_OBJS = $(SERVER_SRCS:.c=.o)
SERVER_BIN  = server

# ── Phase 3: client ───────────────────────────────────────────────────────
CLIENT_SRCS = client.c
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)
CLIENT_BIN  = client

# ── Phase 4: demo test program ───────────────────────────────────────────
DEMO_SRCS = demo.c
DEMO_OBJS = $(DEMO_SRCS:.c=.o)
DEMO_BIN  = demo

# ── Default target ────────────────────────────────────────────────────────
all: $(SHELL_BIN) $(SERVER_BIN) $(CLIENT_BIN) $(DEMO_BIN)

# ── Link Phase 1 shell ────────────────────────────────────────────────────
$(SHELL_BIN): $(SHELL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# ── Link server (must link -lpthread for pthreads and POSIX semaphores) ───
$(SERVER_BIN): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

# ── Link client ───────────────────────────────────────────────────────────
$(CLIENT_BIN): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# ── Link demo ────────────────────────────────────────────────────────────
$(DEMO_BIN): $(DEMO_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# ── Generic rule: compile any .c to a .o ──────────────────────────────────
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Remove all generated files ────────────────────────────────────────────
clean:
	rm -f $(SHELL_OBJS)  $(SHELL_BIN) \
	      $(SERVER_OBJS) $(SERVER_BIN) \
	      $(CLIENT_OBJS) $(CLIENT_BIN) \
	      $(DEMO_OBJS)   $(DEMO_BIN)

.PHONY: all clean
