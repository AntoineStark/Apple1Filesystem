#pragma once
#include "bus.h"
#include "pia.h"
#include "../cpu/fake6502.h"
#include <string>
#include <functional>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Apple1Config – everything needed to configure a machine at startup
// ─────────────────────────────────────────────────────────────────────────────
struct Apple1Config {
    // ROM to load. Two formats:
    //   plain: single ROM file placed at rom_base (e.g. wozmon.rom at 0xFF00)
    //   multibank: path to the 32KA1COMPIL.BIN + .json (set multibank=true)
    std::string rom_path{};
    uint16_t    rom_base{0xFF00};   // ignored for multibank
    bool        multibank{false};
    std::string json_path{};        // only for multibank

    // Optional extra ROM (e.g. BASIC)
    std::string basic_path{};       // if non-empty, loaded at 0xE000

    // Optional RAM preload
    std::string ram_path{};
    uint16_t    ram_base{0x0000};

    // Speed
    bool fast_mode{false};          // run at max speed; false = ~1 MHz

    // Input file (key sequence to inject at startup)
    std::string input_script{};
};

// ─────────────────────────────────────────────────────────────────────────────
// Apple1 – the machine
// ─────────────────────────────────────────────────────────────────────────────
class Apple1 {
public:
    explicit Apple1(Apple1Config cfg);

    // Wire output: emulator calls this for every character sent to the display
    std::function<void(char)> on_output;

    // Feed a character into the keyboard
    void send_key(char c);
    // Feed a string (e.g. from a script file)
    void send_string(const std::string& s);

    // Run N CPU instructions; returns cycles consumed
    int  step();
    // Run for approximately `us` microseconds of simulated time
    void run_us(uint64_t us);
    // Run one full frame (~16.67 ms = 17043 cycles @ 1.022727 MHz)
    void run_frame();
    // Run until stopped (fast mode – no throttling)
    void run_forever(std::function<bool()> should_stop);

    Bus& bus()         { return bus_; }
    PIA& pia()         { return pia_; }
    const fake6502::State& cpu_state() const { return fake6502::state(); }

    // Current active bank index (0 for single-ROM; updated by multibank logic)
    int  current_bank() const { return current_bank_; }

    static constexpr double CLOCK_HZ = 1022727.0; // Apple 1 CPU frequency

private:
    Apple1Config cfg_;
    Bus          bus_;
    PIA          pia_;
    int          current_bank_{0};

    void load_roms();
    void load_multibank();

    // Singleton: fake6502 needs global read/write callbacks
    static Apple1* s_instance;
    static uint8_t s_read (uint16_t addr);
    static void    s_write(uint16_t addr, uint8_t val);
};
