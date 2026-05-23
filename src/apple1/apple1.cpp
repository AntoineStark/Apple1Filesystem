#include "apple1.h"
#include <fstream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

Apple1* Apple1::s_instance = nullptr;

uint8_t Apple1::s_read (uint16_t addr)           { return s_instance->bus_.read(addr); }
void    Apple1::s_write(uint16_t addr, uint8_t v) { s_instance->bus_.write(addr, v);  }

// ─────────────────────────────────────────────────────────────────────────────
Apple1::Apple1(Apple1Config cfg) : cfg_(std::move(cfg)) {
    // Register singleton for CPU callbacks
    s_instance = this;

    // Wire PIA into Bus
    bus_.kbd_read    = [this]{ return pia_.kbd_read();    };
    bus_.kbdcr_read  = [this]{ return pia_.kbdcr_read();  };
    bus_.dsp_read    = [this]{ return pia_.dsp_read();    };
    bus_.dsp_write   = [this](uint8_t v){ pia_.dsp_write(v);   };
    bus_.dspcr_write = [this](uint8_t v){ pia_.dspcr_write(v); };

    // Wire PIA output to our callback
    pia_.output_char = [this](char c){ if (on_output) on_output(c); };

    // Load ROMs
    load_roms();

    // Load optional RAM preload
    if (!cfg_.ram_path.empty()) {
        std::ifstream f(cfg_.ram_path, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open RAM file: " + cfg_.ram_path);
        uint16_t addr = cfg_.ram_base;
        int b;
        while ((b = f.get()) != EOF && addr < Bus::RAM_SIZE)
            bus_.ram[addr++] = (uint8_t)b;
    }

    // Load optional input script
    if (!cfg_.input_script.empty()) {
        std::ifstream f(cfg_.input_script);
        if (!f) throw std::runtime_error("Cannot open input script: " + cfg_.input_script);
        std::string s((std::istreambuf_iterator<char>(f)), {});
        for (char c : s) pia_.enqueue_key(c);
    }

    // Init CPU
    fake6502::set_callbacks(s_read, s_write);
    fake6502::reset();
}

// ─── ROM loading ─────────────────────────────────────────────────────────────
void Apple1::load_roms() {
    if (cfg_.multibank) {
        load_multibank();
    } else if (!cfg_.rom_path.empty()) {
        bus_.load_rom_file(cfg_.rom_base, cfg_.rom_path);
    }

    if (!cfg_.basic_path.empty())
        bus_.load_rom_file(0xE000, cfg_.basic_path);
}

void Apple1::load_multibank() {
    // ── Parse the JSON map ────────────────────────────────────────────────
    std::string json_path = cfg_.json_path;
    if (json_path.empty()) {
        // Derive from ROM path by replacing extension
        json_path = cfg_.rom_path;
        auto dot = json_path.rfind('.');
        if (dot != std::string::npos) json_path = json_path.substr(0, dot);
        json_path += ".json";
    }

    json j;
    {
        std::ifstream jf(json_path);
        if (!jf) throw std::runtime_error("Cannot open ROM JSON: " + json_path);
        jf >> j;
    }

    // Load the 32K binary
    std::ifstream rf(cfg_.rom_path, std::ios::binary);
    if (!rf) throw std::runtime_error("Cannot open ROM binary: " + cfg_.rom_path);
    std::vector<uint8_t> rom_data((std::istreambuf_iterator<char>(rf)), {});
    if (rom_data.size() < 32768)
        rom_data.resize(32768, 0xFF);

    // ── Build mem2rom mapping from "map" field ────────────────────────────
    // "map" is a hex string e.g. "2345789E"
    // Each hex digit i → 4K memory block at (i*0x1000) maps to ROM at (i*0x1000) % 0x8000
    std::string map_str = j.at("map").get<std::string>();

    for (char ch : map_str) {
        int i = (ch >= '0' && ch <= '9') ? ch - '0' :
                (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 :
                (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10 : -1;
        if (i < 0) continue;
        uint16_t mem_base = (uint16_t)(i * 0x1000);
        uint16_t rom_base = mem_base % 0x8000;
        // Extract 4K slice from rom_data starting at rom_base
        std::vector<uint8_t> bank(rom_data.begin() + rom_base,
                                  rom_data.begin() + rom_base + 0x1000);
        bus_.load_rom(mem_base, bank);
    }

    // The loader uses ECHO ($FFEF), PRHEX ($FFE5), PRBYTE ($FFDC) from the
    // standard Apple 1 Wozmon ROM.  Load a compatibility ROM at 0xFF00 that
    // provides exactly those entry points plus the reset vector.
    //
    // Search in the same directory as the 32K binary:  <dir>/apple1_compat.rom
    std::string compat_path = cfg_.rom_path;
    auto slash = compat_path.rfind('/');
    if (slash != std::string::npos)
        compat_path = compat_path.substr(0, slash + 1) + "apple1_compat.rom";
    else
        compat_path = "roms/apple1_compat.rom";

    try {
        bus_.load_rom_file(0xFF00, compat_path);
    } catch (...) {
        // Fall back: just provide the reset/IRQ vectors pointing to the loader
        std::vector<uint8_t> vec_stub = {
            0x00, 0x20,   // 0xFFFA/FB: NMI  → 0x2000
            0x00, 0x20,   // 0xFFFC/FD: RESET→ 0x2000
            0x00, 0x20    // 0xFFFE/FF: IRQ  → 0x2000
        };
        bus_.load_rom(0xFFFA, vec_stub);
    }
}

// ─── Execution ───────────────────────────────────────────────────────────────
void Apple1::send_key(char c)         { pia_.enqueue_key(c); }
void Apple1::send_string(const std::string& s) { for (char c : s) pia_.enqueue_key(c); }

int Apple1::step() {
    int cycles = fake6502::step();
    pia_.tick(cycles);
    return cycles;
}

void Apple1::run_us(uint64_t us) {
    // cycles = us * CLOCK_HZ / 1_000_000
    uint64_t target_cycles = (uint64_t)(us * CLOCK_HZ / 1e6);
    uint64_t done = 0;
    while (done < target_cycles && !cpu_state().stopped)
        done += step();
}

void Apple1::run_frame() {
    // 1 frame ≈ 16,667 µs
    run_us(16667);
}

void Apple1::run_forever(std::function<bool()> should_stop) {
    using clock = std::chrono::steady_clock;

    if (cfg_.fast_mode) {
        while (!should_stop() && !cpu_state().stopped)
            step();
        return;
    }

    // Realtime: run in ~1ms bursts then sleep to maintain ~1 MHz
    while (!should_stop() && !cpu_state().stopped) {
        auto t0 = clock::now();
        run_us(1000); // 1 ms of simulated time
        auto elapsed = clock::now() - t0;
        auto sleep_us = std::chrono::microseconds(1000) - elapsed;
        if (sleep_us > std::chrono::microseconds(0))
            std::this_thread::sleep_for(sleep_us);
    }
}

// ─── State serialisation ─────────────────────────────────────────────────────
// Binary format (little-endian):
//   [0..3]   magic "A1ST"
//   [4..7]   version uint32 = 1
//   [8..9]   PC uint16
//   [10]     SP uint8
//   [11]     A  uint8
//   [12]     X  uint8
//   [13]     Y  uint8
//   [14]     status uint8
//   [15..18] clockticks uint32
//   [19]     stopped bool (as uint8)
//   [20..32787] RAM (32768 bytes, indices 0..32767)
static_assert(sizeof(fake6502::State) >= 9, "State layout check");

void Apple1::save_state(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open state file for writing: " + path);

    const auto& s = fake6502::state();
    uint32_t magic   = 0x54533141; // "A1ST" LE
    uint32_t version = 1;
    f.write(reinterpret_cast<const char*>(&magic),   4);
    f.write(reinterpret_cast<const char*>(&version), 4);
    f.write(reinterpret_cast<const char*>(&s.pc),     2);
    f.write(reinterpret_cast<const char*>(&s.sp),     1);
    f.write(reinterpret_cast<const char*>(&s.a),      1);
    f.write(reinterpret_cast<const char*>(&s.x),      1);
    f.write(reinterpret_cast<const char*>(&s.y),      1);
    f.write(reinterpret_cast<const char*>(&s.status), 1);
    f.write(reinterpret_cast<const char*>(&s.clockticks), 4);
    uint8_t stopped = s.stopped ? 1 : 0;
    f.write(reinterpret_cast<const char*>(&stopped),  1);
    f.write(reinterpret_cast<const char*>(bus_.ram.data()), Bus::RAM_SIZE);
}

void Apple1::load_state(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open state file: " + path);

    uint32_t magic, version;
    f.read(reinterpret_cast<char*>(&magic),   4);
    f.read(reinterpret_cast<char*>(&version), 4);
    if (magic != 0x54533141) throw std::runtime_error("Not an Apple 1 state file: " + path);
    if (version != 1)        throw std::runtime_error("Unsupported state file version");

    fake6502::State s{};
    f.read(reinterpret_cast<char*>(&s.pc),     2);
    f.read(reinterpret_cast<char*>(&s.sp),     1);
    f.read(reinterpret_cast<char*>(&s.a),      1);
    f.read(reinterpret_cast<char*>(&s.x),      1);
    f.read(reinterpret_cast<char*>(&s.y),      1);
    f.read(reinterpret_cast<char*>(&s.status), 1);
    f.read(reinterpret_cast<char*>(&s.clockticks), 4);
    uint8_t stopped = 0;
    f.read(reinterpret_cast<char*>(&stopped),  1);
    s.stopped = (stopped != 0);

    fake6502::restore_state(s);
    f.read(reinterpret_cast<char*>(bus_.ram.data()), Bus::RAM_SIZE);
}
