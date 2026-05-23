#pragma once
#include "../apple1/apple1.h"
#include <string>
#include <deque>

// ─────────────────────────────────────────────────────────────────────────────
// GuiFrontend – ImGui-based Apple 1 emulator UI
//
// Windows:
//   • Terminal    – 40-col Apple 1 screen + keyboard input
//   • CPU State   – registers, flags, cycle count, bank info
//   • Hex Viewer  – 64K memory view with optional 6502 disassembly
//   • Control     – speed, reset, step, breakpoints
// ─────────────────────────────────────────────────────────────────────────────
class GuiFrontend
{
public:
    explicit GuiFrontend(Apple1 &machine);
    ~GuiFrontend();

    // Blocks until window is closed
    void run();

private:
    Apple1 &machine_;

    // Terminal state
    static constexpr int COLS = 40;
    static constexpr int ROWS = 24;
    std::deque<std::string> screen_lines_; // circular text buffer
    std::string current_line_;
    bool cursor_blink_{true};
    double cursor_timer_{0.0};
    int  col_{0};   // current output column (0-39); wraps at 40

    // Hex viewer state
    uint16_t hex_base_addr_{0x0000};
    bool hex_follow_pc_{false};
    bool hex_disasm_{true};

    // Breakpoints
    std::vector<uint16_t> breakpoints_;
    char bp_input_buf_[8]{};

    // Emulator control
    bool running_{true};
    bool step_once_{false};
    double speed_mhz_{1.0}; // target speed (0 = unlimited)

    // GLFW window handle (opaque void* to avoid including glfw here)
    void *window_{nullptr};

    void init_gl();
    void shutdown_gl();
    void draw_frame(double dt);

    void draw_terminal();
    void draw_cpu_state();
    void draw_hex_viewer();
    void draw_control();

    void handle_output(char c);
    void handle_key_callback(int key, int scancode, int action, int mods);

    static GuiFrontend *s_instance;
};
