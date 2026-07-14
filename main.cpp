#include <iostream>
#include <string>
#include <vector>
#include <cctype>

std::vector<std::string> tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    std::string current_token;
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];

        if (c == '\'' && !in_double_quote) {
            // Toggle single quote state
            in_single_quote = !in_single_quote;
            // Note: We deliberately do NOT add the quote character to current_token
        } else if (c == '"' && !in_single_quote) {
            // Toggle double quote state
            in_double_quote = !in_double_quote;
            // Note: We deliberately do NOT add the quote character to current_token
        } else if (std::isspace(c) && !in_single_quote && !in_double_quote) {
            // Unquoted whitespace: end of the current token
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
        } else {
            // Any other character belongs to the current token
            current_token += c;
        }
    }

    // Edge case: User typed a quote but didn't close it (e.g., `echo "hello`)
    if (in_single_quote || in_double_quote) {
        std::cerr << "myshell: syntax error: unterminated quote\n";
        return {}; // Return empty vector to discard the line
    }

    // Add the final token if the string didn't end with a space
    if (!current_token.empty()) {
        tokens.push_back(current_token);
    }

    return tokens;
}

int main() {
    std::string input;

    // The REPL (Read-Eval-Print Loop)
    while (true) {
        // Print the prompt
        std::cout << "myshell> ";
        std::cout.flush(); // Ensure the prompt is printed immediately

        // Read input
        if (!std::getline(std::cin, input)) {
            // EOF reached (e.g., Ctrl+D)
            std::cout << "\n";
            break;
        }

        // If the user just pressed Enter, do nothing and prompt again
        if (input.empty()) {
            continue;
        }

        // Tokenize the input string
        std::vector<std::string> tokens = tokenize(input);

        // Eval/Print (Placeholder for future milestones)
        // For Milestone 2: just print the parsed tokens to verify correctness
        if (!tokens.empty()) {
            for (size_t i = 0; i < tokens.size(); ++i) {
                std::cout << "Token " << i << ": [" << tokens[i] << "]\n";
            }
        }
    }

    return 0;
}
