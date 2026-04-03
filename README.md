# MyShell - A Unix Shell with Remote Socket Support

A Unix shell implementation in C supporting command execution, pipelines, I/O redirection,
and remote access via TCP socket communication.

## Features

- **Command Execution**: Execute standard Unix commands with arguments
- **Pipelines**: Chain multiple commands using `|`
- **Input Redirection**: Redirect input from files using `<`
- **Output Redirection**: Redirect output to files using `>`
- **Error Redirection**: Redirect stderr to files using `2>`
- **Quoted Arguments**: Single and double quote handling with backslash escaping
- **Remote Shell**: Execute commands over a TCP socket connection (Phase 2)
- **Exit Command**: Type `exit` or press Ctrl+D to quit

---

## Project Structure

```
myshell/
├── main.c          — Phase 1 interactive shell entry point
├── input.c/h       — User input reading module
├── parse.c/h       — Command parser: tokenization, pipes, redirections
├── execute.c/h     — Pipeline executor: fork, execvp, dup2, waitpid
├── shell.c/h       — Phase 2 bridge: captures command output as a string
├── server.c        — Phase 2 TCP server
├── client.c        — Phase 2 TCP client
└── Makefile        — Builds myshell, server, and client
```

---

## Building

```bash
cd myshell
make
```

This produces three executables: `myshell`, `server`, and `client`.

To remove compiled files:

```bash
make clean
```

---

## Phase 1 — Interactive Shell

Run the local interactive shell:

```bash
./myshell
```

### Examples

```bash
$ ls -la
$ echo "Hello World"
$ ls | grep .c
$ cat file.txt | grep pattern | wc -l
$ ls > output.txt
$ wc -w < input.txt
$ ls nonexistent 2> error.log
```

---

## Phase 2 — Remote Shell via TCP Socket

The server executes commands using the Phase 1 shell engine. The client connects to it,
sends commands, and displays the output — identical to a local shell from the user's perspective.

**Start the server** (Terminal 1):

```bash
./server
```

**Start the client** (Terminal 2):

```bash
./client
```

Type commands at the `$` prompt. Type `exit` to disconnect.

### Server Output Format

```
[INFO] Server started, waiting for client connections...
[INFO] Client connected.
[RECEIVED] Received command: "ls -l" from client.
[EXECUTING] Executing command: "ls -l"
[OUTPUT] Sending output to client:
...

[RECEIVED] Received command: "unknowncmd" from client.
[EXECUTING] Executing command: "unknowncmd"
[ERROR] Command not found: "unknowncmd"
[OUTPUT] Sending error message to client: Error: Command not found: unknowncmd

[INFO] Client disconnected.
```

---

## Implementation Details

- `fork()` and `execvp()` for process creation
- `pipe()` and `dup2()` for inter-process communication and I/O redirection
- `socket()`, `bind()`, `listen()`, `accept()`, `recv()`, `send()` for TCP communication
- Phase 1 parser and executor reused unchanged in the Phase 2 server
- Output capture via fork+pipe wrapper in `shell.c`
- `SO_REUSEADDR` set on server socket for immediate restart after shutdown

## Limitations

- Maximum input length: 1024 characters
- Maximum arguments per command: 64
- Maximum commands in pipeline: 32
- Server handles one client connection per session
- No background process support (`&`)
- No environment variable expansion or command history

## Authors

Bernard Gharbin (bg2696) and Bismark Buernortey Buer (bb3621)  
Operating Systems — NYU Abu Dhabi
