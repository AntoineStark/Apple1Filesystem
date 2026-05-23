#include "terminal_frontend.h"
#include <cstdio>
#include <cstring>
#include <csignal>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

// ─── Signal handling ─────────────────────────────────────────────────────────
static std::atomic<bool>* g_quit_ptr = nullptr;
static void sigint_handler(int) { if (g_quit_ptr) g_quit_ptr->store(true); }

// ─── Terminal raw-mode ───────────────────────────────────────────────────────
static struct termios g_orig_termios;
static bool           g_terminal_saved = false;

TerminalFrontend::TerminalFrontend(Apple1& machine) : machine_(machine) {
    // Wire display output
    machine_.on_output = [this](char c){ handle_output(c); };

    g_quit_ptr = &quit_;
    signal(SIGINT, sigint_handler);
}

TerminalFrontend::~TerminalFrontend() {
    restore_terminal();
    signal(SIGINT, SIG_DFL);
}

void TerminalFrontend::setup_terminal() {
    if (!isatty(STDIN_FILENO)) return; // script / pipe mode – don't touch tty
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    g_terminal_saved = true;

    struct termios raw = g_orig_termios;
    // Raw mode: no echo, no canonical, no signals from Ctrl-C (we handle it manually)
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN]  = 0; // non-blocking
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

void TerminalFrontend::restore_terminal() {
    if (g_terminal_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
        g_terminal_saved = false;
    }
}

// ─── Input polling ───────────────────────────────────────────────────────────
void TerminalFrontend::poll_input() {
    char buf[16];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    for (ssize_t i = 0; i < n; ++i) {
        char c = buf[i];
        if (c == 3) { quit_.store(true); return; } // Ctrl-C
        if (c == 127) c = 0x08; // DEL → backspace for Apple 1
        if (c == '\n') c = '\r'; // newline → CR
        machine_.send_key(c);
    }
}

// ─── Output ──────────────────────────────────────────────────────────────────
void TerminalFrontend::handle_output(char c) {
    c &= 0x7F; // strip bit 7 (Apple 1 sets it)
    if (c == 0x0D) {                        // CR → newline on modern terminals
        fputc('\n', stdout);
    } else if (c >= 0x20 && c < 0x7F) {    // printable ASCII
        fputc(c, stdout);
    } else if (c == 0x08) {                 // backspace
        fputs("\b \b", stdout);
    }
    fflush(stdout);
}

// ─── Main loop ───────────────────────────────────────────────────────────────
void TerminalFrontend::run() {
    setup_terminal();

    machine_.run_forever([this]() -> bool {
        poll_input();
        return quit_.load();
    });

    restore_terminal();
    if (quit_.load()) fputc('\n', stdout);
}
