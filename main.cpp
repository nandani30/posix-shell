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
#include <termios.h>
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <dirent.h>
#include <cstring>

// --- Job Control Globals ---
struct Job {
    int id;
    pid_t pgid;
    std::string command;
    std::string status; 
};
std::vector<Job> job_table;
int next_job_id = 1;
pid_t shell_pgid;
int shell_terminal;
int shell_is_interactive;

// --- History, Terminal & Polish Globals ---
std::vector<std::string> history;
struct termios orig_termios;
bool raw_mode_enabled = false;
std::unordered_map<std::string, std::string> aliases;

std::string get_prompt() {
    char cwd[1024];
    std::string prompt_str = "";
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        std::string cwd_str(cwd);
        const char* home = getenv("HOME");
        if (home && cwd_str.find(home) == 0) {
            cwd_str.replace(0, strlen(home), "~");
        }
        prompt_str += "\033[1;36m[" + cwd_str + "]\033[0m ";
    }
    
    FILE* pipe = popen("git rev-parse --abbrev-ref HEAD 2>/dev/null", "r");
    if (pipe) {
        char buffer[128];
        std::string result = "";
        while (!feof(pipe)) {
            if (fgets(buffer, 128, pipe) != NULL)
                result += buffer;
        }
        pclose(pipe);
        if (!result.empty()) {
            result.erase(result.find_last_not_of(" \n\r\t") + 1);
            prompt_str += "\033[1;35m(" + result + ")\033[0m ";
        }
    }
    prompt_str += "myshell> ";
    return prompt_str;
}

std::vector<std::string> get_completions(const std::string& prefix, bool is_first_word) {
    std::vector<std::string> matches;
    if (is_first_word) {
        std::vector<std::string> builtins = {"cd", "exit", "help", "pwd", "export", "jobs", "fg", "bg", "alias", "unalias"};
        for (const auto& b : builtins) {
            if (b.find(prefix) == 0) matches.push_back(b);
        }
        for (const auto& p : aliases) {
            if (p.first.find(prefix) == 0) matches.push_back(p.first);
        }
        const char* path_env = getenv("PATH");
        if (path_env) {
            std::string path_str = path_env;
            size_t start = 0;
            size_t end = path_str.find(':');
            while (end != std::string::npos || start < path_str.length()) {
                std::string dir;
                if (end != std::string::npos) {
                    dir = path_str.substr(start, end - start);
                    start = end + 1;
                    end = path_str.find(':', start);
                } else {
                    dir = path_str.substr(start);
                    start = path_str.length();
                }
                
                DIR* dp = opendir(dir.c_str());
                if (dp) {
                    struct dirent* ep;
                    while ((ep = readdir(dp))) {
                        std::string name = ep->d_name;
                        if (name == "." || name == "..") continue;
                        if (name.find(prefix) == 0) matches.push_back(name);
                    }
                    closedir(dp);
                }
            }
        }
        std::sort(matches.begin(), matches.end());
        matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    } else {
        size_t slash = prefix.find_last_of('/');
        std::string dir_path = ".";
        std::string search_prefix = prefix;
        if (slash != std::string::npos) {
            dir_path = prefix.substr(0, slash);
            search_prefix = prefix.substr(slash + 1);
            if (dir_path.empty()) dir_path = "/";
        }
        
        DIR* dp = opendir(dir_path.c_str());
        if (dp) {
            struct dirent* ep;
            while ((ep = readdir(dp))) {
                std::string name = ep->d_name;
                if (name == "." || name == "..") continue;
                if (name.find(search_prefix) == 0) {
                    if (slash != std::string::npos) {
                        matches.push_back(prefix.substr(0, slash + 1) + name);
                    } else {
                        matches.push_back(name);
                    }
                }
            }
            closedir(dp);
        }
    }
    return matches;
}

void load_history() {
    const char* home = getenv("HOME");
    if (!home) return;
    std::string path = std::string(home) + "/.myshell_history";
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) history.push_back(line);
    }
}

void save_history() {
    const char* home = getenv("HOME");
    if (!home) return;
    std::string path = std::string(home) + "/.myshell_history";
    std::ofstream file(path);
    for (const auto& line : history) {
        file << line << "\n";
    }
}

void disable_raw_mode() {
    if (raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_mode_enabled = false;
    }
}

void enable_raw_mode() {
    if (!shell_is_interactive) return;
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_mode_enabled = true;
}

bool read_line_with_history(std::string& input, const std::string& prompt) {
    if (!shell_is_interactive) {
        std::cout << prompt;
        std::cout.flush();
        return (bool)std::getline(std::cin, input);
    }

    input.clear();
    int history_idx = history.size(); 
    std::string current_buffer = "";
    
    std::cout << prompt;
    std::cout.flush();
    
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\n' || c == '\r') {
            std::cout << "\n";
            return true;
        } else if (c == 3) { // Ctrl+C
            std::cout << "^C\n";
            input.clear();
            return true; 
        } else if (c == 4) { // Ctrl+D
            if (input.empty()) return false;
        } else if (c == 127 || c == 8) { // Backspace
            if (!input.empty()) {
                input.pop_back();
                std::cout << "\b \b";
                std::cout.flush();
            }
        } else if (c == 9) { // Tab
            size_t last_space = input.find_last_of(" \t");
            bool is_first = (last_space == std::string::npos);
            std::string current_word = is_first ? input : input.substr(last_space + 1);
            
            std::vector<std::string> matches = get_completions(current_word, is_first);
            if (matches.size() == 1) {
                std::string match = matches[0];
                std::string remainder = match.substr(current_word.length());
                if (!is_first || (is_first && matches[0].find('/') == std::string::npos)) {
                    remainder += " ";
                }
                input += remainder;
                std::cout << remainder;
                std::cout.flush();
            }
        } else if (c == '\x1b') { 
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) == 0) continue;
            if (read(STDIN_FILENO, &seq[1], 1) == 0) continue;
            
            if (seq[0] == '[') {
                if (seq[1] == 'A') { // Up
                    if (history_idx > 0) {
                        if (history_idx == (int)history.size()) current_buffer = input; 
                        history_idx--;
                        for (size_t i = 0; i < input.length(); ++i) std::cout << "\b \b";
                        input = history[history_idx];
                        std::cout << input;
                        std::cout.flush();
                    }
                } else if (seq[1] == 'B') { // Down
                    if (history_idx < (int)history.size()) {
                        history_idx++;
                        for (size_t i = 0; i < input.length(); ++i) std::cout << "\b \b";
                        if (history_idx == (int)history.size()) input = current_buffer;
                        else input = history[history_idx];
                        std::cout << input;
                        std::cout.flush();
                    }
                }
            }
        } else if (c >= 32 && c <= 126) { 
            input += c;
            std::cout << c;
            std::cout.flush();
        }
    }
    return false;
}

void add_job(pid_t pgid, const std::string& command, const std::string& status) {
    job_table.push_back({next_job_id++, pgid, command, status});
}

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
        save_history();
        disable_raw_mode();
        exit(0);
    } 
    else if (cmd == "help") {
        std::cout << "myshell - A custom Unix shell\n";
        std::cout << "Built-in commands:\n";
        std::cout << "  cd [dir]       - Change the current directory\n";
        std::cout << "  pwd            - Print the current working directory\n";
        std::cout << "  export KEY=VAL - Set an environment variable\n";
        std::cout << "  jobs           - List background and stopped jobs\n";
        std::cout << "  fg %N          - Bring job N to foreground\n";
        std::cout << "  bg %N          - Resume job N in background\n";
        std::cout << "  alias n='c'    - Set an alias\n";
        std::cout << "  unalias n      - Remove an alias\n";
        std::cout << "  help           - Show this help message\n";
        std::cout << "  exit           - Exit the shell\n";
        return 0;
    } 
    else if (cmd == "alias") {
        if (tokens.size() == 1) {
            for (const auto& pair : aliases) {
                std::cout << "alias " << pair.first << "='" << pair.second << "'\n";
            }
        } else {
            std::string arg = tokens[1];
            size_t eq_pos = arg.find('=');
            if (eq_pos != std::string::npos) {
                std::string name = arg.substr(0, eq_pos);
                std::string val = arg.substr(eq_pos + 1);
                if (val.length() >= 2 && ((val.front() == '\'' && val.back() == '\'') || (val.front() == '"' && val.back() == '"'))) {
                    val = val.substr(1, val.length() - 2);
                }
                aliases[name] = val;
            } else {
                std::cerr << "myshell: alias: invalid format (expected name='command')\n";
                return 1;
            }
        }
        return 0;
    }
    else if (cmd == "unalias") {
        if (tokens.size() < 2) {
            std::cerr << "myshell: unalias: missing argument\n";
            return 1;
        }
        aliases.erase(tokens[1]);
        return 0;
    }
    else if (cmd == "jobs") {
        for (const auto& job : job_table) {
            std::cout << "[" << job.id << "] " << job.status << "\t\t" << job.command << "\n";
        }
        return 0;
    }
    else if (cmd == "fg" || cmd == "bg") {
        if (tokens.size() < 2 || tokens[1][0] != '%') {
            std::cerr << "myshell: " << cmd << ": usage: " << cmd << " %N\n";
            return 1;
        }
        int job_id = std::stoi(tokens[1].substr(1));
        
        auto it = std::find_if(job_table.begin(), job_table.end(), [job_id](const Job& j) { return j.id == job_id; });
        if (it == job_table.end()) {
            std::cerr << "myshell: " << cmd << ": %" << job_id << ": no such job\n";
            return 1;
        }
        
        pid_t pgid = it->pgid;
        std::string command = it->command;
        
        if (cmd == "fg") {
            it->status = "Running";
            std::cout << command << "\n";
            
            if (shell_is_interactive) {
                tcsetpgrp(shell_terminal, pgid);
            }
            
            kill(-pgid, SIGCONT);
            
            int status;
            waitpid(pgid, &status, WUNTRACED);
            if (WIFSTOPPED(status)) {
                std::cout << "\n[" << job_id << "]+  Stopped                 " << command << "\n";
                it->status = "Stopped";
            } else {
                job_table.erase(it);
            }
            
            if (shell_is_interactive) {
                tcsetpgrp(shell_terminal, shell_pgid);
            }
            return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        } else {
            it->status = "Running";
            std::cout << "[" << job_id << "]+ " << command << " &\n";
            kill(-pgid, SIGCONT);
            return 0;
        }
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
    return 127; 
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
    std::string cmd_str = "";

    for (size_t i = 0; i < tokens.size(); ++i) {
        cmd_str += tokens[i] + " ";
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

    const std::string& cmd = tokens[0];
    if (!in_child_process) {
        if (cmd == "cd" || cmd == "exit" || cmd == "help" || cmd == "pwd" || cmd == "export" || cmd == "jobs" || cmd == "fg" || cmd == "bg" || cmd == "alias" || cmd == "unalias") {
            return execute_builtin(tokens);
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "myshell: fork failed\n";
        return 1;
    } else if (pid == 0) {
        if (shell_is_interactive) {
            pid_t child_pid = getpid();
            setpgid(child_pid, child_pid);
            if (!background) {
                tcsetpgrp(shell_terminal, child_pid);
            }
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
        }

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
        if (cmd == "cd" || cmd == "exit" || cmd == "help" || cmd == "pwd" || cmd == "export" || cmd == "jobs" || cmd == "fg" || cmd == "bg" || cmd == "alias" || cmd == "unalias") {
            exit(execute_builtin(clean_tokens));
        }

        if (execvp(args[0], args.data()) == -1) {
            std::cerr << "myshell: " << args[0] << ": command not found\n";
            exit(127); 
        }
        exit(1);
    } else {
        if (shell_is_interactive) {
            setpgid(pid, pid);
        }

        if (!background) {
            if (shell_is_interactive) {
                tcsetpgrp(shell_terminal, pid);
            }
            
            int status;
            waitpid(pid, &status, WUNTRACED);
            
            if (shell_is_interactive) {
                tcsetpgrp(shell_terminal, shell_pgid);
            }

            if (WIFSTOPPED(status)) {
                std::cout << "\n[" << next_job_id << "]+  Stopped                 " << cmd_str << "\n";
                add_job(pid, cmd_str, "Stopped");
                return 148; 
            } else if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }
            return 1;
        } else {
            add_job(pid, cmd_str, "Running");
            std::cout << "[" << (next_job_id - 1) << "] " << pid << "\n";
            return 0; 
        }
    }
}

int execute_pipeline(const std::vector<std::string>& tokens, bool background) {
    std::vector<std::vector<std::string>> commands;
    std::vector<std::string> current_command;
    std::string full_cmd_str = "";

    for (const auto& token : tokens) {
        full_cmd_str += token + " ";
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
    pid_t pgid = 0;

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
            if (shell_is_interactive) {
                pid_t child_pid = getpid();
                if (pgid == 0) pgid = child_pid;
                setpgid(child_pid, pgid);
                if (!background) {
                    tcsetpgrp(shell_terminal, pgid);
                }
                signal(SIGINT, SIG_DFL);
                signal(SIGQUIT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);
                signal(SIGTTIN, SIG_DFL);
                signal(SIGTTOU, SIG_DFL);
            }

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
            if (shell_is_interactive) {
                if (pgid == 0) pgid = pid;
                setpgid(pid, pgid);
            }
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
        if (shell_is_interactive) {
            tcsetpgrp(shell_terminal, pgid);
        }

        for (pid_t pid : pids) {
            int status;
            waitpid(pid, &status, WUNTRACED);
            if (WIFSTOPPED(status)) {
                if (pid == pids.back()) {
                    std::cout << "\n[" << next_job_id << "]+  Stopped                 " << full_cmd_str << "\n";
                    add_job(pgid, full_cmd_str, "Stopped");
                    final_status = 148;
                }
            } else if (pid == pids.back()) {
                if (WIFEXITED(status)) final_status = WEXITSTATUS(status);
                else final_status = 1;
            }
        }

        if (shell_is_interactive) {
            tcsetpgrp(shell_terminal, shell_pgid);
        }
    } else {
        add_job(pgid, full_cmd_str, "Running");
        std::cout << "[" << (next_job_id - 1) << "] " << pids.back() << "\n";
    }
    return final_status;
}

int execute_chains(const std::vector<std::string>& tokens, bool background) {
    bool has_chain = false;
    for (const auto& t : tokens) {
        if (t == "&&" || t == "||" || t == ";") { has_chain = true; break; }
    }

    // If no chain operators, let execute_pipeline handle backgrounding natively
    if (!has_chain) {
        return execute_pipeline(tokens, background);
    }

    // If there ARE chain operators AND it's a background task, 
    // we fork a subshell to manage the sequential chain asynchronously.
    if (background) {
        std::string full_cmd_str = "";
        for (const auto& t : tokens) full_cmd_str += t + " ";
        full_cmd_str += "&";
        
        pid_t pid = fork();
        if (pid == 0) {
            shell_is_interactive = 0; // Disable terminal hijacking in the background
            setpgid(0, 0); 
            exit(execute_chains(tokens, false)); // Execute synchronously in child
        } else if (pid > 0) {
            setpgid(pid, pid);
            add_job(pid, full_cmd_str, "Running");
            std::cout << "[" << (next_job_id - 1) << "] " << pid << "\n";
            return 0;
        } else {
            perror("myshell: fork failed");
            return 1;
        }
    }

    std::vector<std::string> current_chunk;
    int last_status = 0;
    bool skip_chunk = false;

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "&&" || tokens[i] == "||" || tokens[i] == ";") {
            if (!current_chunk.empty()) {
                if (!skip_chunk) {
                    last_status = execute_pipeline(current_chunk, false); // Always false here!
                }
                current_chunk.clear();
            }

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
        last_status = execute_pipeline(current_chunk, false);
    }

    return last_status;
}

int main() {
    shell_terminal = STDIN_FILENO;
    shell_is_interactive = isatty(shell_terminal);

    if (shell_is_interactive) {
        while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);

        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);

        shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) < 0) {
            perror("Couldn't put the shell in its own process group");
            exit(1);
        }

        tcsetpgrp(shell_terminal, shell_pgid);
        load_history();
    }

    while (true) {
        int status;
        pid_t zombie_pid;
        while ((zombie_pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
            auto it = std::find_if(job_table.begin(), job_table.end(), 
                [zombie_pid](const Job& j) { return j.pgid == zombie_pid || j.pgid == getpgid(zombie_pid); });
            
            if (it != job_table.end()) {
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    std::cout << "\n[" << it->id << "]+  Done                    " << it->command << "\n";
                    job_table.erase(it);
                } else if (WIFSTOPPED(status)) {
                    it->status = "Stopped";
                }
            }
        }

        std::string full_input;
        std::string input;
        
        enable_raw_mode();
        bool ok = read_line_with_history(input, get_prompt());
        disable_raw_mode();
        
        if (!ok) {
            std::cout << "\n";
            break;
        }
        
        full_input = input;

        char open_quote = get_open_quote(full_input);
        while (open_quote != '\0') {
            std::string next_line;
            enable_raw_mode();
            bool cont_ok = read_line_with_history(next_line, (open_quote == '"') ? "dquote> " : "quote> ");
            disable_raw_mode();
            
            if (!cont_ok) {
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
        
        if (shell_is_interactive) {
            history.push_back(full_input);
        }

        std::vector<std::string> tokens = tokenize(full_input);
        
        // Alias Expansion
        if (!tokens.empty()) {
            auto it = aliases.find(tokens[0]);
            if (it != aliases.end()) {
                std::vector<std::string> expanded = tokenize(it->second);
                tokens.erase(tokens.begin());
                tokens.insert(tokens.begin(), expanded.begin(), expanded.end());
            }
        }

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

    if (shell_is_interactive) {
        save_history();
    }
    return 0;
}
