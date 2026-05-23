#include "apple1/apple1.h"
#include "frontend/terminal_frontend.h"
#include "frontend/gui_frontend.h"
#include <cstdio>
#include <cstring>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Usage
// ─────────────────────────────────────────────────────────────────────────────
static void print_usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --rom <file>          ROM file to load (default: roms/wozmon.rom)\n"
        "  --rom-base <addr>     ROM base address in hex (default: 0xFF00)\n"
        "  --basic <file>        BASIC ROM to load at 0xE000\n"
        "  --multibank           Treat --rom as a 32K multi-bank ROM (apple1loader)\n"
        "  --json <file>         JSON map for multi-bank ROM (default: derived from --rom)\n"
        "  --ram <file>          Binary file to preload into RAM\n"
        "  --ram-base <addr>     RAM load address in hex (default: 0x0000)\n"
        "  --script <file>       Key sequence file to inject at startup\n"
        "  --fast                Run at maximum speed (no realtime throttling)\n"
        "  --gui                 Launch ImGui graphical frontend\n"
        "  --help                Show this help\n"
        "\n"
        "Examples:\n"
        "  %s --rom roms/wozmon_mini.rom\n"
        "  %s --rom roms/wozmon_mini.rom --basic roms/basic.rom\n"
        "  %s --rom roms/32KA1COMPIL.BIN --multibank\n"
        "  %s --rom roms/wozmon_mini.rom --script tests/hello.txt --fast\n"
        "  %s --gui --rom roms/32KA1COMPIL.BIN --multibank\n"
        "\n"
        "Controls (terminal):\n"
        "  Ctrl-C    quit\n"
        "Controls (GUI):\n"
        "  F5        pause/resume\n"
        "  F6        single step\n"
        "  F7        reset\n"
        "  Ctrl-C    quit\n",
        prog, prog, prog, prog, prog, prog);
}

static uint16_t parse_hex(const char* s) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    return (uint16_t)std::stoul(s, nullptr, 16);
}

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    Apple1Config cfg;
    cfg.rom_path  = "roms/wozmon_mini.rom";
    cfg.rom_base  = 0xFF00;
    bool use_gui  = false;
    std::string load_state_path;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            print_usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--rom") && i+1 < argc) {
            cfg.rom_path = argv[++i];
        } else if (!strcmp(argv[i], "--rom-base") && i+1 < argc) {
            cfg.rom_base = parse_hex(argv[++i]);
        } else if (!strcmp(argv[i], "--basic") && i+1 < argc) {
            cfg.basic_path = argv[++i];
        } else if (!strcmp(argv[i], "--multibank")) {
            cfg.multibank = true;
        } else if (!strcmp(argv[i], "--json") && i+1 < argc) {
            cfg.json_path = argv[++i];
        } else if (!strcmp(argv[i], "--ram") && i+1 < argc) {
            cfg.ram_path = argv[++i];
        } else if (!strcmp(argv[i], "--ram-base") && i+1 < argc) {
            cfg.ram_base = parse_hex(argv[++i]);
        } else if (!strcmp(argv[i], "--script") && i+1 < argc) {
            cfg.input_script = argv[++i];
        } else if (!strcmp(argv[i], "--fast")) {
            cfg.fast_mode = true;
        } else if (!strcmp(argv[i], "--gui")) {
            use_gui = true;
        } else if (!strcmp(argv[i], "--load-state") && i+1 < argc) {
            load_state_path = argv[++i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    try {
        Apple1 machine(cfg);
        if (!load_state_path.empty())
            machine.load_state(load_state_path);

        if (use_gui) {
            GuiFrontend frontend(machine);
            frontend.run();
        } else {
            TerminalFrontend frontend(machine);
            frontend.run();
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
    return 0;
}
