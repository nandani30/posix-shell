#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include <fcntl.h>
#include <csignal>

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
        } else if (c == '$' && !in_single_quote) {
            // Variable expansion (ignored if inside single quotes)
            std::string var_name;
            size_t j = i + 1;
            // Variable names can contain alphanumeric characters and underscores
            while (j < input.length() && (std::isalnum(input[j]) || input[j] == '_')) {
                var_name += input[j];
                j++;
            }
            
            if (!var_name.empty()) {
                const char* val = getenv(var_name.c_str());
                if (val != nullptr) {
                    current_token += val; // Append the expanded value
                }
                i = j - 1; // Advance the outer loop iterator past the variable name
            } else {
                // A lone '$' sign
                current_token += '$';
            }
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
        std::cout << "  cd [dir]       - Change the current directory\n";
        std::cout << "  pwd            - Print the current working directory\n";
        std::cout << "  export KEY=VAL - Set an environment variable\n";
        std::cout << "  help           - Show this help message\n";
        std::cout << "  exit           - Exit the shell\n";
        return true;
    } 
    else if (cmd == "export") {
        if (tokens.size() < 2) {
            std::cerr << "myshell: export: missing argument\n";
        } else {
            std::string arg = tokens[1];
            size_t eq_pos = arg.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = arg.substr(0, eq_pos);
                std::string value = arg.substr(eq_pos + 1);
                // setenv sets the environment variable in the current process
                // 1 means overwrite if it already exists
                if (setenv(key.c_str(), value.c_str(), 1) != 0) {
                    perror("myshell: export");
                }
            } else {
                std::cerr << "myshell: export: invalid format (expected KEY=VALUE)\n";
            }
        }
        return true;
    }
    else if (cmd == "cd") {
        std::string target_dir;
        if (tokens.size() < 2) {
            // Default to $HOME if no arguments are provided
            const char* home = getenv("HOME");
            if (home != nullptr) {
                target_dir = home;
            } else {
                std::cerr << "myshell: cd: HOME not set\n";
                return true;
            }
        } else {
            target_dir = tokens[1];
        }

        if (chdir(target_dir.c_str()) != 0) {
            perror("myshell: cd");
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

void execute_single_command(const std::vector<std::string>& tokens, bool in_child_process) {
    if (tokens.empty()) {
        if (in_child_process) exit(0);
        return;
    }

    std::vector<char*> args;
    std::string input_file = "";
    std::string output_file = "";
    bool append_output = false;

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "<") {
            if (i + 1 < tokens.size()) { input_file = tokens[i + 1]; ++i; }
            else { std::cerr << "myshell: syntax error\n"; if (in_child_process) exit(1); return; }
        } else if (tokens[i] == ">") {
            if (i + 1 < tokens.size()) { output_file = tokens[i + 1]; append_output = false; ++i; }
            else { std::cerr << "myshell: syntax error\n"; if (in_child_process) exit(1); return; }
        } else if (tokens[i] == ">>") {
            if (i + 1 < tokens.size()) { output_file = tokens[i + 1]; append_output = true; ++i; }
            else { std::cerr << "myshell: syntax error\n"; if (in_child_process) exit(1); return; }
        } else {
            args.push_back(const_cast<char*>(tokens[i].c_str()));
        }
    }
    
    if (args.empty()) {
        if (in_child_process) exit(0);
        return;
    }
    args.push_back(nullptr);

    auto do_exec = [&]() {
        signal(SIGINT, SIG_DFL);

        if (!input_file.empty()) {
            int fd0 = open(input_file.c_str(), O_RDONLY);
            if (fd0 < 0) { perror("myshell"); exit(1); }
            dup2(fd0, STDIN_FILENO);
            close(fd0);
        }
        if (!output_file.empty()) {
            int flags = O_WRONLY | O_CREAT | (append_output ? O_APPEND : O_TRUNC);
            int fd1 = open(output_file.c_str(), flags, 0644);
            if (fd1 < 0) { perror("myshell"); exit(1); }
            dup2(fd1, STDOUT_FILENO);
            close(fd1);
        }

        std::vector<std::string> clean_tokens;
        for (int i = 0; args[i] != nullptr; ++i) clean_tokens.push_back(args[i]);
        if (execute_builtin(clean_tokens)) {
            exit(0);
        }

        if (execvp(args[0], args.data()) == -1) {
            std::cerr << "myshell: " << args[0] << ": command not found\n";
            exit(1); 
        }
    };

    if (in_child_process) {
        do_exec();
    } else {
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "myshell: fork failed\n";
        } else if (pid == 0) {
            do_exec();
        } else {
            int status;
            waitpid(pid, &status, 0);
        }
    }
}

void execute_pipeline(const std::vector<std::string>& tokens) {
    std::vector<std::vector<std::string>> commands;
    std::vector<std::string> current_command;

    for (const auto& token : tokens) {
        if (token == "|") {
            if (!current_command.empty()) {
                commands.push_back(current_command);
                current_command.clear();
            } else {
                std::cerr << "myshell: syntax error near unexpected token `|'\n";
                return;
            }
        } else {
            current_command.push_back(token);
        }
    }
    if (!current_command.empty()) {
        commands.push_back(current_command);
    }

    if (commands.empty()) return;

    if (commands.size() == 1) {
        const std::string& cmd = commands[0][0];
        if (cmd == "cd" || cmd == "exit" || cmd == "help" || cmd == "pwd" || cmd == "export") {
            execute_builtin(commands[0]);
            return;
        }
        execute_single_command(commands[0], false);
        return;
    }

    int prev_pipe_read_fd = -1;
    std::vector<pid_t> pids;

    for (size_t i = 0; i < commands.size(); ++i) {
        int pipefd[2];
        if (i < commands.size() - 1) {
            if (pipe(pipefd) < 0) {
                perror("myshell: pipe failed");
                return;
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("myshell: fork failed");
            return;
        }

        if (pid == 0) {
            if (prev_pipe_read_fd != -1) {
                dup2(prev_pipe_read_fd, STDIN_FILENO);
                close(prev_pipe_read_fd);
            }
            if (i < commands.size() - 1) {
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]); 
                close(pipefd[1]); 
            }
            
            execute_single_command(commands[i], true);
            exit(1);
        } else {
            pids.push_back(pid);
            if (prev_pipe_read_fd != -1) {
                close(prev_pipe_read_fd);
            }
            if (i < commands.size() - 1) {
                close(pipefd[1]); 
                prev_pipe_read_fd = pipefd[0];
            }
        }
    }

    for (pid_t pid : pids) {
        waitpid(pid, nullptr, 0);
    }
}

int main() {
    signal(SIGINT, SIG_IGN);
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
            execute_pipeline(tokens);
        }
    }

    return 0;
}
