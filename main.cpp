#include <iostream>
#include <string>

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

        // Eval/Print (Placeholder for future milestones)
        // For now, just echo back the input
        std::cout << "You entered: " << input << "\n";
    }

    return 0;
}
