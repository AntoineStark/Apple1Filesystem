#pragma once
#include <cstdint>
#include <array>
#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Bus – Apple 1 memory bus
//
// Address space:
//   0x0000–0x7FFF  RAM (32K max; typically only 4 or 8K populated)
//   0xD010         KBD   – keyboard data
//   0xD011         KBDCR – keyboard control (bit7 = key ready)
//   0xD012         DSP   – display output  (bit7 = busy)
//   0xD013         DSPCR – display control (write-only)
//   0xE000–0xEFFF  ROM bank (BASIC, or part of 32K card)
//   0xF000–0xFFFF  ROM bank (Wozmon at 0xFF00, or part of 32K card)
// ─────────────────────────────────────────────────────────────────────────────

struct RomBank {
    uint16_t base{};              // start address in 6502 space
    std::vector<uint8_t> data{};  // ROM contents
};

class Bus {
public:
    // Callbacks for PIA I/O (set by PIA before emulation starts)
    std::function<uint8_t()>       kbd_read;       // read KBD data
    std::function<uint8_t()>       kbdcr_read;     // read KBDCR
    std::function<uint8_t()>       dsp_read;       // read DSP (busy bit)
    std::function<void(uint8_t)>   dsp_write;      // write to display
    std::function<void(uint8_t)>   dspcr_write;    // write control

    // RAM: 32K (only lower portion is "real"; upper is ignored in plain mode)
    static constexpr uint32_t RAM_SIZE = 32768;
    std::array<uint8_t, RAM_SIZE> ram{};

    // ROM banks (loaded from files)
    std::vector<RomBank> rom_banks{};

    void load_rom(uint16_t base_addr, const std::vector<uint8_t>& data);
    void load_rom_file(uint16_t base_addr, const std::string& path);

    uint8_t read(uint16_t addr);
    void    write(uint16_t addr, uint8_t val);

private:
    const uint8_t* find_rom(uint16_t addr) const;
};
