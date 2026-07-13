# myshell

A custom Unix shell written in C++ on macOS.

This project is built incrementally to demonstrate systems programming concepts, focusing on POSIX system calls and modern C++ practices.

## Milestones

### Milestone 1: Foundation (The REPL)

The core of any shell is the Read-Eval-Print Loop (REPL). It repeatedly:
1. **Reads** input from the user.
2. **Evaluates** (parses and executes) the input.
3. **Prints** the result (if any).

In this milestone, we establish the basic loop.

#### OS Concepts & Syscalls Demonstrated
- **Standard Streams**: Uses `std::cin` (wrapper around file descriptor 0, STDIN) and `std::cout` (file descriptor 1, STDOUT).
- **EOF (End of File)**: When a user presses `Ctrl+D` in a Unix terminal, it signals an EOF on the standard input stream. `std::getline` detects this and returns `false`, allowing the shell to exit cleanly.

## Building and Running

Ensure you have a C++ compiler installed (e.g., `clang++` via Xcode Command Line Tools).

```bash
make
./myshell
```

To exit the shell, press `Ctrl+D`.

### Milestone 2: Tokenizer/Parser

Before a command can be executed, the raw string input must be broken down into discrete "tokens". 

#### OS Concepts & Syscalls Demonstrated
- **State Machine Parsing**: A naive `split(" ")` function fails when spaces are inside quotes (e.g., `echo "hello world"` should be two tokens: `echo` and `hello world`). To solve this, we implemented a state machine that iterates character-by-character, tracking whether we are currently `in_single_quote` or `in_double_quote`. This preserves spaces inside quotes.
### Milestone 3: Command Execution (fork, exec, wait)

With tokens in hand, the shell can execute commands using the foundational POSIX process model.

#### OS Concepts & Syscalls Demonstrated
- **`fork()`**: This system call creates an exact duplicate of the shell process. The original process is the "parent", and the new duplicate is the "child". They have identical memory at the exact moment of the fork, but proceed independently. `fork()` returns `0` to the child, and the child's Process ID (PID) to the parent, which is how we write the `if`/`else` branching logic.
- **`execvp()`**: Called inside the child process. It replaces the child's memory image (our shell code) entirely with the new program (e.g., `/bin/ls`). If `execvp` succeeds, it never returns. If it returns, an error occurred (like command not found). The `p` in `execvp` means it will automatically search the user's `$PATH` for the executable, and the `v` means it takes an array of arguments (vector).
### Milestone 4: Built-in Commands

While `execvp` can run external binaries (like `/bin/ls`), certain commands *must* be implemented directly within the shell process itself.

#### OS Concepts & Syscalls Demonstrated
- **Built-in vs External**: Why can't `cd` be an external program? If we `fork()` a child process and the child runs a `cd` binary, the child's working directory changes. But when the child dies and control returns to the parent (our shell), the parent's directory remains unchanged! Therefore, the shell process itself must call the `chdir()` system call.
- **Process Termination**: Similarly, `exit` cannot be an external command. The shell must call the `exit(0)` syscall itself to terminate its own process.
### Milestone 5: I/O Redirection

A crucial feature of any shell is the ability to redirect the standard input (`<`) and standard output (`>`, `>>`) of processes to and from files.

#### OS Concepts & Syscalls Demonstrated
- **File Descriptors (FDs)**: Every process has a table of open files. By default, `0` is STDIN, `1` is STDOUT, and `2` is STDERR. 
- **`open()`**: Used to open a file. For `>`, we pass flags `O_WRONLY | O_CREAT | O_TRUNC` to truncate the file. For `>>`, we pass `O_APPEND`.
- **`dup2(oldfd, newfd)`**: This is the magic behind redirection. `dup2` forces `newfd` to point to the exact same file as `oldfd`. 
  - To redirect output, we open a file (which gets a random FD like `3`), and call `dup2(3, 1)`. 
  - Now, whenever the program (like `echo`) writes to STDOUT (`1`), the OS sends that data to FD `3` (our file).
### Milestone 6: Piping

The pipe operator (`|`) allows chaining commands together, passing the standard output of one directly into the standard input of the next.

#### OS Concepts & Syscalls Demonstrated
- **Inter-Process Communication (IPC)**: The `pipe(fd)` syscall creates an in-memory buffer with two ends: a read end (`fd[0]`) and a write end (`fd[1]`). This allows completely separate processes to communicate with each other.
- **Dynamic Piping**: By using a loop, we can chain an arbitrary number of commands together. For `N` commands, we create `N-1` pipes.
### Milestone 7: Signal Handling (SIGINT)

A robust shell should not crash when the user presses `Ctrl+C`. Instead, it should interrupt whatever foreground program is running.

#### OS Concepts & Syscalls Demonstrated
- **Signals and `Ctrl+C`**: When you press `Ctrl+C`, the terminal driver sends a `SIGINT` (Interrupt) signal to the foreground process group.
- **`signal()`**: The `signal` syscall allows a process to change how it responds to signals. We set `SIGINT` to `SIG_IGN` (ignore) in the main shell process so that the shell itself never dies from `Ctrl+C`.
### Milestone 8: Environment Variables ($HOME, export)

We have added support for accessing and modifying environment variables, a crucial part of shell configuration.

#### OS Concepts & Syscalls Demonstrated
- **The Environment Block**: Every process has an environment—an array of key-value string pairs (like `PATH=/usr/bin`). When `fork()` is called, the child inherits an exact copy of the parent's environment.
- **`getenv()` and `setenv()`**: We use `getenv("HOME")` to look up variables during tokenization. We built the `export` command using `setenv()` to modify the shell's own environment block. Any child process spawned *after* `export` is called will inherit the newly modified environment!
### Milestone 9: Wildcard Globbing (`*.cpp`)

Our shell now automatically expands wildcards before executing commands.

#### OS Concepts & Syscalls Demonstrated
### Milestone 10: Background Processes (`&`)

The final feature of our shell allows executing long-running tasks in the background, instantly returning the prompt to the user.

#### OS Concepts & Syscalls Demonstrated
- **Asynchronous Execution**: By passing a `background` flag to our execution logic, the parent shell process simply skips the `waitpid()` blocking call. This allows the shell to immediately print the prompt and accept new input while the child process runs concurrently.
- **Zombie Processes**: When a child process terminates, the operating system keeps a tiny amount of information (its exit status) in memory so the parent can read it later using `waitpid()`. If the parent never calls `waitpid()`, this data is never cleaned up, resulting in a "zombie" process.
- **Zombie Reaping with `WNOHANG`**: To prevent our background processes from turning into permanent zombies, we added a cleanup loop at the top of our REPL: `waitpid(-1, &status, WNOHANG)`. 
### Rigorous Testing

No systems programming project is complete without edge-case testing. We built an automated test suite (`test.sh`) to throw malformed input at the shell to ensure it fails gracefully rather than crashing (segfaulting).

#### Tests Included:
- **Parser Edge Cases**: Unterminated quotes, excessive whitespace.
- **Pipeline Edge Cases**: Malformed pipes (`ls | `, `| ls`, `ls | | wc`).
- **Redirection Edge Cases**: Missing target files for `<` and `>`.
- **Variable Expansion**: Missing environment variables and quote interpolation.
- **Stress Testing**: Running massive pipelines to ensure our `pipe()` and `dup2()` logic rigorously closes all file descriptors and prevents "Too many open files" limits from being breached.

### Bonus Stretch Goal: Multi-line Quotes (`dquote>`)

To match the behavior of real shells like `zsh` and `bash`, we overhauled the REPL input loop. If a user types a command with an unclosed single or double quote and presses Enter, the shell no longer crashes with a syntax error. 

Instead, it evaluates the string using a state machine and detects the unclosed quote. It then spawns a continuation prompt (`dquote> ` or `quote> `) and dynamically accumulates input line-by-line until the user finally closes the quote. Only then is the massive multi-line string passed to the Tokenizer and execution pipeline!

### Milestone 11: Command Chaining (`&&`, `||`, `;`)

We upgraded the shell from a blind execution engine to a Logic Router. The shell can now run multiple commands sequentially and conditionally based on the success or failure of previous commands.

#### OS Concepts & Syscalls Demonstrated
- **Exit Status Propagation**: When an application finishes, it returns an integer code to the OS (0 for success, 1-255 for errors). We updated our `waitpid()` logic to extract this exact integer using `WIFEXITED(status)` and `WEXITSTATUS(status)`. 
- **Short-Circuit Evaluation**: 
  - `&&` requires the previous command to return `0` (Success). If it fails, the shell skips the next command entirely.
  - `||` requires the previous command to return non-zero (Failure). If it succeeds, the shell skips the fallback command.
  - `;` runs the next command unconditionally.

### Milestone 12: Real Job Control (`jobs`, `fg`, `bg`, `Ctrl+Z`)

We implemented true POSIX Job Control, which is widely considered the most difficult systems programming challenge for a custom shell. 

#### OS Concepts & Syscalls Demonstrated
- **Process Groups (`setpgid`)**: Instead of launching standalone child processes, the shell now groups pipelines into a single "Process Group". This allows signals to be broadcast to all processes in a pipeline simultaneously.
- **Terminal Ownership (`tcsetpgrp`)**: If a process group is running in the foreground, the shell explicitly hands over ownership of the terminal keyboard input to that process group. Once the process group finishes or is paused, the shell snatches ownership back to print its prompt.
- **Signal Handling & `WUNTRACED`**: We ignore terminal signals (`SIGTSTP`, `SIGTTIN`, `SIGTTOU`) in the shell itself. When `Ctrl+Z` is pressed, the foreground process group receives the `SIGTSTP` signal and pauses. Our `waitpid()` loop uses the `WUNTRACED` flag to detect this pause, adds the job to an internal `job_table`, and safely returns you to the prompt!

### Milestone 13: Persistent History & Arrow Keys

To make the shell feel like a polished, daily-driver environment, we implemented persistent history and arrow-key recall.

#### OS Concepts & Syscalls Demonstrated
- **Raw Terminal Mode (`termios`)**: By default, macOS terminals use "Canonical Mode," which buffers keystrokes until the user presses Enter. We used `tcgetattr` and `tcsetattr` to strip this buffering and force the terminal into "Raw Mode" (`~ICANON`). This allows our C++ code to intercept every single keystroke byte-by-byte instantly.
- **Custom Input Loop**: Because we disabled the OS buffer, we had to write our own `read_line_with_history` function to handle backspaces, manual cursor screen erasure (`\b \b`), and parsing the ANSI escape sequences for the Up and Down arrow keys (`\x1b[A`).
- **File I/O Persistence**: History is loaded from `~/.myshell_history` on startup and flushed back to disk on `exit`, ensuring your commands survive across reboots.

### Milestone 14: Polish (Custom Prompts, Aliases, Tab Completion)

The final milestone focuses on ergonomic UX features that make the shell feel like a professional tool.

#### Features Implemented
- **Dynamic Prompt**: The shell dynamically reads your Current Working Directory (`getcwd`), detects if you are in your home folder (rendering `~`), and quietly shells out to `git rev-parse` to append your active Git branch to the prompt in cyan/magenta ANSI colors!
- **Aliases**: You can map custom shortcuts using `alias ll='ls -la'`. The shell expands these tokens recursively right before parsing.
- **Tab Completion**: By intercepting the ASCII `9` (Tab) key in Raw Mode, the shell automatically searches your `built-ins`, `$PATH` executables, and the files in your current directory using `<dirent.h>`. If it finds a unique match, it types the rest of the word for you!
