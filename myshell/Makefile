CC     = gcc
CFLAGS = -Wall -Wextra -std=c99 -g

# Phase 1: interactive shell
SRCS   = main.c input.c parse.c execute.c
OBJS   = $(SRCS:.c=.o)
TARGET = myshell

# Phase 2: socket shell
SERVER_SRCS = server.c shell.c parse.c execute.c
CLIENT_SRCS = client.c

all: $(TARGET) server client

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

server: $(SERVER_SRCS)
	$(CC) $(CFLAGS) -o server $(SERVER_SRCS)

client: $(CLIENT_SRCS)
	$(CC) $(CFLAGS) -o client $(CLIENT_SRCS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) server client

.PHONY: all clean
