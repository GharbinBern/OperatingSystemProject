# MyShell - A Simple Unix Shell

A simple Unix shell implementation in C that supports basic command execution, pipelines, and I/O redirection.

## Features

- **Command Execution**: Execute standard Unix commands
- **Pipelines**: Chain multiple commands using pipes (`|`)
- **Input Redirection**: Redirect input from files using `<`
- **Output Redirection**: Redirect output to files using `>`
- **Error Redirection**: Redirect stderr to files using `2>`
- **Interactive Mode**: Read-eval-print loop with command prompt
- **Exit Command**: Type `exit` or press Ctrl+D to quit

## Building

```bash
cd myshell
make
```

## Usage

Run the shell:
```bash
./myshell
```

### Examples

Basic command:
```bash
$ ls -la
```

Pipeline:
```bash
$ ls | grep myshell
```

Output redirection:
```bash
$ echo "Hello World" > output.txt
```

Input redirection:
```bash
$ wc -l < input.txt
```

Error redirection:
```bash
$ ls nonexistent 2> error.log
```

Complex pipeline:
```bash
$ cat file.txt | grep pattern | sort | uniq
```

## Project Structure

- **main.c**: Main shell loop and command prompt
- **input.c/h**: User input reading module
- **parse.c/h**: Command parsing and pipeline construction
- **execute.c/h**: Process creation, execution, and I/O redirection
- **Makefile**: Build configuration

## Implementation Details

- Uses `fork()` and `execvp()` for process creation
- Implements inter-process communication using `pipe()`
- File descriptor manipulation with `dup2()` for redirections
- Proper error handling and resource cleanup
- Memory management with dynamic allocation

## Limitations

- Maximum input length: 1024 characters
- Maximum arguments per command: 64
- Maximum commands in pipeline: 32
- No support for background processes (`&`)
- No support for environment variable expansion
- No support for command history

## Author

Built as an Operating Systems course project.
