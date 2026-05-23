#pragma once
#include "../apple1/apple1.h"
#include <atomic>

// ─────────────────────────────────────────────────────────────────────────────
// TerminalFrontend – runs an Apple1 in the terminal
//
// • Sets stdin to raw/non-blocking mode
// • Forwards typed keys to the Apple1 PIA
// • Prints Apple1 output to stdout
// • Ctrl-C quits
// ─────────────────────────────────────────────────────────────────────────────
class TerminalFrontend {
public:
    explicit TerminalFrontend(Apple1& machine);
    ~TerminalFrontend();

    // Blocks until the machine stops or the user presses Ctrl-C
    void run();

private:
    Apple1&            machine_;
    std::atomic<bool>  quit_{false};

    void setup_terminal();
    void restore_terminal();
    void poll_input();
    void handle_output(char c);
};
