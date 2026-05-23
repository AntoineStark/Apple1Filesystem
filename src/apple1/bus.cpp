#include "bus.h"
#include <fstream>
#include <stdexcept>

void Bus::load_rom(uint16_t base_addr, const std::vector<uint8_t>& data) {
    rom_banks.push_back({base_addr, data});
}

void Bus::load_rom_file(uint16_t base_addr, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open ROM: " + path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    load_rom(base_addr, data);
}

const uint8_t* Bus::find_rom(uint16_t addr) const {
    // Search newest banks first so later loads can shadow earlier ones
    for (auto it = rom_banks.rbegin(); it != rom_banks.rend(); ++it) {
        const auto& b = *it;
        if (addr >= b.base && addr < b.base + (uint16_t)b.data.size())
            return &b.data[addr - b.base];
    }
    return nullptr;
}

uint8_t Bus::read(uint16_t addr) {
    // PIA registers
    if (addr == 0xD010) return kbd_read   ? kbd_read()   : 0;
    if (addr == 0xD011) return kbdcr_read ? kbdcr_read() : 0;
    if (addr == 0xD012) return dsp_read   ? dsp_read()   : 0;
    if (addr == 0xD013) return 0;

    // ROM banks
    if (const uint8_t* p = find_rom(addr)) return *p;

    // RAM (mirror the upper half back to RAM for addresses >= RAM_SIZE)
    return ram[addr % RAM_SIZE];
}

void Bus::write(uint16_t addr, uint8_t val) {
    // PIA registers
    if (addr == 0xD012) { if (dsp_write)   dsp_write(val);   return; }
    if (addr == 0xD013) { if (dspcr_write) dspcr_write(val); return; }
    if (addr == 0xD010 || addr == 0xD011)  return; // read-only from CPU side

    // ROM is read-only (silently ignore writes)
    if (find_rom(addr)) return;

    // RAM
    ram[addr % RAM_SIZE] = val;
}
