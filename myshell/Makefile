# Makefile — Phase 3 Multithreaded Shell Server
#
# Separate compilation is used throughout:
#   - Each .c file is compiled to its own .o object file.
#   - The linker then combines the object files into the final binary.
# This means a change to one .c file only recompiles that one file, not
# the entire project.
#
# Targets:
#   all      – build myshell (Phase 1 interactive shell), server, and client
#   myshell  – Phase 1 interactive shell binary
#   server   – Phase 3 multithreaded TCP shell server
#   client   – TCP client that connects to the server
#   clean    – remove all object files and binaries

CC     = gcc

# -Wall/-Wextra:           enable most useful warnings
# -std=c99:                C99 standard (with _POSIX_C_SOURCE for POSIX extensions)
# -D_POSIX_C_SOURCE=200809L: expose POSIX.1-2008 APIs (strdup, strerror, etc.) on Linux
# -g:                      include debug symbols (useful on the remote server)
CFLAGS = -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L -g

# ── Phase 1: interactive shell ────────────────────────────────────────────
SHELL_SRCS = main.c input.c parse.c execute.c
SHELL_OBJS = $(SHELL_SRCS:.c=.o)
SHELL_BIN  = myshell

# ── Phase 3: server (requires -lpthread for POSIX threads on Linux) ──────
SERVER_SRCS = server.c shell.c parse.c execute.c
SERVER_OBJS = $(SERVER_SRCS:.c=.o)
SERVER_BIN  = server

# ── Phase 3: client ───────────────────────────────────────────────────────
CLIENT_SRCS = client.c
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)
CLIENT_BIN  = client

# ── Default target ────────────────────────────────────────────────────────
all: $(SHELL_BIN) $(SERVER_BIN) $(CLIENT_BIN)

# ── Link Phase 1 shell ────────────────────────────────────────────────────
$(SHELL_BIN): $(SHELL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# ── Link server (must link -lpthread for pthread_create, sem_init, etc.) ──
$(SERVER_BIN): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

# ── Link client ───────────────────────────────────────────────────────────
$(CLIENT_BIN): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# ── Generic rule: compile any .c to a .o ──────────────────────────────────
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Remove all generated files ────────────────────────────────────────────
clean:
	rm -f $(SHELL_OBJS) $(SHELL_BIN) \
	      $(SERVER_OBJS) $(SERVER_BIN) \
	      $(CLIENT_OBJS) $(CLIENT_BIN)

.PHONY: all clean
