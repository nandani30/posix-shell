#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <unistd.h>     // For fork(), execvp(), chdir(), getcwd()
#include <sys/wait.h>   // For waitpid()
#include <cstdlib>      // For exit()

std::vector<std::string> tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    std::string current_token;
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];

        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        } else if (std::isspace(c) && !in_single_quote && !in_double_quote) {
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
        } else {
            current_token += c;
        }
    }

    if (in_single_quote || in_double_quote) {
        std::cerr << "myshell: syntax error: unterminated quote\n";
        return {};
    }

    if (!current_token.empty()) {
        tokens.push_back(current_token);
    }

    return tokens;
}

// Function to handle built-in commands
// Returns true if a built-in was executed (or attempted), false if it's not a built-in
bool execute_builtin(const std::vector<std::string>& tokens) {
    if (tokens.empty()) return false;
    const std::string& cmd = tokens[0];

    if (cmd == "exit") {
        // Exit the shell process gracefully
        std::cout << "Exiting myshell...\n";
        exit(0);
    } 
    else if (cmd == "help") {
        std::cout << "myshell - A custom Unix shell\n";
        std::cout << "Built-in commands:\n";
        std::cout << "  cd [dir] - Change the current directory\n";
        std::cout << "  pwd      - Print the current working directory\n";
        std::cout << "  help     - Show this help message\n";
        std::cout << "  exit     - Exit the shell\n";
        return true;
    } 
    else if (cmd == "cd") {
        if (tokens.size() < 2) {
            std::cerr << "myshell: cd: missing argument\n";
        } else {
            // chdir changes the current working directory of the calling process
            if (chdir(tokens[1].c_str()) != 0) {
                // perror prints the given string followed by the system error message (e.g., "No such file or directory")
                perror("myshell: cd");
            }
        }
        return true;
    } 
    else if (cmd == "pwd") {
        char cwd[1024];
        // getcwd fills the buffer with the absolute path of the current working directory
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            std::cout << cwd << "\n";
        } else {
            perror("myshell: pwd");
        }
        return true;
    }

    return false;
}

void execute_command(const std::vector<std::string>& tokens) {
    if (tokens.empty()) return;

    // Convert vector of std::string to array of char* for execvp
    std::vector<char*> args;
    for (const auto& token : tokens) {
        args.push_back(const_cast<char*>(token.c_str()));
    }
    args.push_back(nullptr); // The array must be null-terminated

    // fork() creates a new process by duplicating the calling process
    pid_t pid = fork();

    if (pid < 0) {
        // Error occurred during fork
        std::cerr << "myshell: fork failed\n";
    } else if (pid == 0) {
        // We are in the child process
        // execvp replaces the current process image with a new process image
        if (execvp(args[0], args.data()) == -1) {
            // If execvp returns, an error occurred
            std::cerr << "myshell: " << args[0] << ": command not found\n";
            exit(1); // Exit the child process immediately
        }
    } else {
        // We are in the parent process (the shell)
        // Wait for the specific child process (pid) to change state (finish)
        int status;
        waitpid(pid, &status, 0);
    }
}

int main() {
    std::string input;

    while (true) {
        std::cout << "myshell> ";
        std::cout.flush();

        if (!std::getline(std::cin, input)) {
            std::cout << "\n";
            break;
        }

        if (input.empty()) {
            continue;
        }

        std::vector<std::string> tokens = tokenize(input);

        if (!tokens.empty()) {
            // First, check if the command is a built-in
            if (!execute_builtin(tokens)) {
                // If not a built-in, execute it as an external command
                execute_command(tokens);
            }
        }
    }

    return 0;
}
