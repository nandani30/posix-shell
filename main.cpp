#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <unistd.h>     // For fork(), execvp(), chdir(), getcwd(), dup2()
#include <sys/wait.h>   // For waitpid()
#include <cstdlib>      // For exit()
#include <fcntl.h>      // For open() and O_* flags

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

bool execute_builtin(const std::vector<std::string>& tokens) {
    if (tokens.empty()) return false;
    const std::string& cmd = tokens[0];

    if (cmd == "exit") {
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
            if (chdir(tokens[1].c_str()) != 0) {
                perror("myshell: cd");
            }
        }
        return true;
    } 
    else if (cmd == "pwd") {
        char cwd[1024];
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

    std::vector<char*> args;
    std::string input_file = "";
    std::string output_file = "";
    bool append_output = false;

    // Scan for redirection operators and build the clean args array
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "<") {
            if (i + 1 < tokens.size()) {
                input_file = tokens[i + 1];
                ++i; // skip the filename token
            } else {
                std::cerr << "myshell: syntax error near unexpected token `newline'\n";
                return;
            }
        } else if (tokens[i] == ">") {
            if (i + 1 < tokens.size()) {
                output_file = tokens[i + 1];
                append_output = false;
                ++i;
            } else {
                std::cerr << "myshell: syntax error near unexpected token `newline'\n";
                return;
            }
        } else if (tokens[i] == ">>") {
            if (i + 1 < tokens.size()) {
                output_file = tokens[i + 1];
                append_output = true;
                ++i;
            } else {
                std::cerr << "myshell: syntax error near unexpected token `newline'\n";
                return;
            }
        } else {
            args.push_back(const_cast<char*>(tokens[i].c_str()));
        }
    }
    
    // Nothing left to execute (e.g., user just typed `> file.txt`)
    if (args.empty()) return;

    args.push_back(nullptr); // Null-terminate for execvp

    pid_t pid = fork();

    if (pid < 0) {
        std::cerr << "myshell: fork failed\n";
    } else if (pid == 0) {
        // --- CHILD PROCESS ---
        
        // Handle input redirection `<`
        if (!input_file.empty()) {
            int fd0 = open(input_file.c_str(), O_RDONLY);
            if (fd0 < 0) {
                perror("myshell");
                exit(1);
            }
            dup2(fd0, STDIN_FILENO); // Replace standard input with our file
            close(fd0);              // Clean up the original file descriptor
        }

        // Handle output redirection `>` or `>>`
        if (!output_file.empty()) {
            int flags = O_WRONLY | O_CREAT | (append_output ? O_APPEND : O_TRUNC);
            // 0644 gives rw-r--r-- permissions to the newly created file
            int fd1 = open(output_file.c_str(), flags, 0644);
            if (fd1 < 0) {
                perror("myshell");
                exit(1);
            }
            dup2(fd1, STDOUT_FILENO); // Replace standard output with our file
            close(fd1);               // Clean up the original file descriptor
        }

        // Now execute the command. Its stdin/stdout are pointing to the files (if redirected)
        if (execvp(args[0], args.data()) == -1) {
            std::cerr << "myshell: " << args[0] << ": command not found\n";
            exit(1); 
        }
    } else {
        // --- PARENT PROCESS ---
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
            if (!execute_builtin(tokens)) {
                execute_command(tokens);
            }
        }
    }

    return 0;
}
