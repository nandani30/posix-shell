#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include <fcntl.h>
#include <csignal>
#include <glob.h>

char get_open_quote(const std::string& input) {
    bool in_single_quote = false;
    bool in_double_quote = false;
    for (char c : input) {
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        }
    }
    if (in_single_quote) return '\'';
    if (in_double_quote) return '"';
    return '\0';
}

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
            std::string var_name;
            size_t j = i + 1;
            while (j < input.length() && (std::isalnum(input[j]) || input[j] == '_')) {
                var_name += input[j];
                j++;
            }
            
            if (!var_name.empty()) {
                const char* val = getenv(var_name.c_str());
                if (val != nullptr) {
                    current_token += val; 
                }
                i = j - 1; 
            } else {
                current_token += '$';
            }
        } else if (c == '&' && !in_single_quote && !in_double_quote) {
            if (i + 1 < input.length() && input[i+1] == '&') {
                if (!current_token.empty()) { tokens.push_back(current_token); current_token.clear(); }
                tokens.push_back("&&");
                i++;
            } else {
                if (!current_token.empty()) { tokens.push_back(current_token); current_token.clear(); }
                tokens.push_back("&");
            }
        } else if (c == '|' && !in_single_quote && !in_double_quote) {
            if (i + 1 < input.length() && input[i+1] == '|') {
                if (!current_token.empty()) { tokens.push_back(current_token); current_token.clear(); }
                tokens.push_back("||");
                i++;
            } else {
                if (!current_token.empty()) { tokens.push_back(current_token); current_token.clear(); }
                tokens.push_back("|");
            }
        } else if (c == ';' && !in_single_quote && !in_double_quote) {
            if (!current_token.empty()) { tokens.push_back(current_token); current_token.clear(); }
            tokens.push_back(";");
        } else if (std::isspace(c) && !in_single_quote && !in_double_quote) {
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
        } else {
            current_token += c;
        }
    }

    if (!current_token.empty()) {
        tokens.push_back(current_token);
    }

    return tokens;
}

std::vector<std::string> expand_globs(const std::vector<std::string>& tokens) {
    std::vector<std::string> expanded_tokens;
    for (const auto& token : tokens) {
        if (token.find('*') != std::string::npos || 
            token.find('?') != std::string::npos || 
            (!token.empty() && token[0] == '~')) {
            glob_t glob_result;
            if (glob(token.c_str(), GLOB_NOCHECK | GLOB_TILDE, nullptr, &glob_result) == 0) {
                for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
                    expanded_tokens.push_back(std::string(glob_result.gl_pathv[i]));
                }
                globfree(&glob_result);
            } else {
                expanded_tokens.push_back(token); 
            }
        } else {
            expanded_tokens.push_back(token);
        }
    }
    return expanded_tokens;
}

int execute_builtin(const std::vector<std::string>& tokens) {
    if (tokens.empty()) return 1;
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
        return 0;
    } 
    else if (cmd == "export") {
        if (tokens.size() < 2) {
            std::cerr << "myshell: export: missing argument\n";
            return 1;
        } else {
            std::string arg = tokens[1];
            size_t eq_pos = arg.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = arg.substr(0, eq_pos);
                std::string value = arg.substr(eq_pos + 1);
                if (setenv(key.c_str(), value.c_str(), 1) != 0) {
                    perror("myshell: export");
                    return 1;
                }
            } else {
                std::cerr << "myshell: export: invalid format (expected KEY=VALUE)\n";
                return 1;
            }
        }
        return 0;
    }
    else if (cmd == "cd") {
        std::string target_dir;
        if (tokens.size() < 2) {
            const char* home = getenv("HOME");
            if (home != nullptr) {
                target_dir = home;
            } else {
                std::cerr << "myshell: cd: HOME not set\n";
                return 1;
            }
        } else {
            target_dir = tokens[1];
        }

        if (chdir(target_dir.c_str()) != 0) {
            perror("myshell: cd");
            return 1;
        }
        return 0;
    } 
    else if (cmd == "pwd") {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            std::cout << cwd << "\n";
            return 0;
        } else {
            perror("myshell: pwd");
            return 1;
        }
    }
    return 127; // Command not found
}

int execute_single_command(const std::vector<std::string>& tokens, bool in_child_process, bool background) {
    if (tokens.empty()) {
        if (in_child_process) exit(0);
        return 0;
    }

    std::vector<char*> args;
    std::string input_file = "";
    std::string output_file = "";
    bool append_output = false;

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "<") {
            if (i + 1 < tokens.size()) { input_file = tokens[i + 1]; ++i; }
            else { std::cerr << "myshell: syntax error\n"; if (in_child_process) exit(1); return 1; }
        } else if (tokens[i] == ">") {
            if (i + 1 < tokens.size()) { output_file = tokens[i + 1]; append_output = false; ++i; }
            else { std::cerr << "myshell: syntax error\n"; if (in_child_process) exit(1); return 1; }
        } else if (tokens[i] == ">>") {
            if (i + 1 < tokens.size()) { output_file = tokens[i + 1]; append_output = true; ++i; }
            else { std::cerr << "myshell: syntax error\n"; if (in_child_process) exit(1); return 1; }
        } else {
            args.push_back(const_cast<char*>(tokens[i].c_str()));
        }
    }
    
    if (args.empty()) {
        if (in_child_process) exit(0);
        return 0;
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
        
        const std::string& cmd = clean_tokens[0];
        if (cmd == "cd" || cmd == "exit" || cmd == "help" || cmd == "pwd" || cmd == "export") {
            exit(execute_builtin(clean_tokens));
        }

        if (execvp(args[0], args.data()) == -1) {
            std::cerr << "myshell: " << args[0] << ": command not found\n";
            exit(127); 
        }
    };

    if (in_child_process) {
        do_exec();
        return 0; // Should never reach here
    } else {
        const std::string& cmd = tokens[0];
        // Execute builtin in parent process if it's the only command
        if (cmd == "cd" || cmd == "exit" || cmd == "help" || cmd == "pwd" || cmd == "export") {
            return execute_builtin(tokens);
        }

        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "myshell: fork failed\n";
            return 1;
        } else if (pid == 0) {
            do_exec();
            exit(1);
        } else {
            if (!background) {
                int status;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status)) {
                    return WEXITSTATUS(status);
                }
                return 1;
            } else {
                std::cout << "[Background] PID " << pid << "\n";
                return 0; // Background process technically detached, assume 0 for immediate return
            }
        }
    }
}

int execute_pipeline(const std::vector<std::string>& tokens, bool background) {
    std::vector<std::vector<std::string>> commands;
    std::vector<std::string> current_command;

    for (const auto& token : tokens) {
        if (token == "|") {
            if (!current_command.empty()) {
                commands.push_back(current_command);
                current_command.clear();
            } else {
                std::cerr << "myshell: syntax error near unexpected token `|'\n";
                return 1;
            }
        } else {
            current_command.push_back(token);
        }
    }
    if (!current_command.empty()) {
        commands.push_back(current_command);
    } else if (!tokens.empty() && tokens.back() == "|") {
        std::cerr << "myshell: syntax error near unexpected token `|'\n";
        return 1;
    }

    if (commands.empty()) return 0;

    if (commands.size() == 1) {
        return execute_single_command(commands[0], false, background);
    }

    int prev_pipe_read_fd = -1;
    std::vector<pid_t> pids;

    for (size_t i = 0; i < commands.size(); ++i) {
        int pipefd[2];
        if (i < commands.size() - 1) {
            if (pipe(pipefd) < 0) {
                perror("myshell: pipe failed");
                return 1;
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("myshell: fork failed");
            return 1;
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
            
            int status = execute_single_command(commands[i], true, background);
            exit(status);
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

    int final_status = 0;
    if (!background) {
        for (pid_t pid : pids) {
            int status;
            waitpid(pid, &status, 0);
            if (pid == pids.back()) {
                if (WIFEXITED(status)) final_status = WEXITSTATUS(status);
                else final_status = 1;
            }
        }
    } else {
        if (!pids.empty()) {
            std::cout << "[Background] Pipeline PID " << pids.back() << "\n";
        }
    }
    return final_status;
}

int execute_chains(const std::vector<std::string>& tokens, bool background) {
    std::vector<std::string> current_chunk;
    std::string next_op = "";
    int last_status = 0;
    bool skip_chunk = false;

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "&&" || tokens[i] == "||" || tokens[i] == ";") {
            if (!current_chunk.empty()) {
                if (!skip_chunk) {
                    last_status = execute_pipeline(current_chunk, background);
                }
                current_chunk.clear();
            }

            // Decide whether to skip the next chunk based on last_status
            if (tokens[i] == "&&") {
                skip_chunk = (last_status != 0);
            } else if (tokens[i] == "||") {
                skip_chunk = (last_status == 0);
            } else if (tokens[i] == ";") {
                skip_chunk = false;
            }
        } else {
            current_chunk.push_back(tokens[i]);
        }
    }

    if (!current_chunk.empty() && !skip_chunk) {
        last_status = execute_pipeline(current_chunk, background);
    }

    return last_status;
}

int main() {
    signal(SIGINT, SIG_IGN);

    while (true) {
        int status;
        pid_t zombie_pid;
        while ((zombie_pid = waitpid(-1, &status, WNOHANG)) > 0) {
            std::cout << "[Background] PID " << zombie_pid << " finished.\n";
        }

        std::string full_input;
        std::string input;
        
        std::cout << "myshell> ";
        std::cout.flush();

        if (!std::getline(std::cin, input)) {
            std::cout << "\n";
            break;
        }
        
        full_input = input;

        char open_quote = get_open_quote(full_input);
        while (open_quote != '\0') {
            if (open_quote == '"') std::cout << "dquote> ";
            else std::cout << "quote> ";
            std::cout.flush();
            
            std::string next_line;
            if (!std::getline(std::cin, next_line)) {
                std::cout << "\nmyshell: unexpected EOF while looking for matching `" << open_quote << "'\n";
                break; 
            }
            full_input += "\n" + next_line;
            open_quote = get_open_quote(full_input);
        }

        if (open_quote != '\0') {
            break;
        }

        if (full_input.empty()) {
            continue;
        }

        std::vector<std::string> tokens = tokenize(full_input);
        tokens = expand_globs(tokens);

        if (!tokens.empty()) {
            bool background = false;
            if (tokens.back() == "&") {
                background = true;
                tokens.pop_back();
            }
            
            if (!tokens.empty()) {
                execute_chains(tokens, background);
            }
        }
    }

    return 0;
}
