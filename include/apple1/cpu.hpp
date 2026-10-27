#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace apple1 {

// Instruction-granularity NMOS 6502. Bus accesses are not cycle exact:
// dummy reads/writes and interrupt polling races are not modeled.
class Cpu {
public:
    std::function<uint8_t(uint16_t)> read;
    std::function<void(uint16_t, uint8_t)> write;
    uint8_t a = 0, x = 0, y = 0, sp = 0xfd, p = 0x24;
    uint16_t pc = 0;
    uint64_t cycles = 0;
    bool irq = false, nmi = false;

    // Deterministic cold reset: clears A/X/Y and interrupt inputs, sets SP/P
    // to FD/24, loads FFFC/FFFD, and starts the cycle counter at seven.
    void reset();
    // IRQ is level-sensitive. Set nmi to latch an edge; service clears it.
    // B is a stack-image flag only; live P keeps bit 5 set and bit 4 clear.
    unsigned step();
    static std::string disassemble(
        uint16_t address, const std::function<uint8_t(uint16_t)>& peek);
};

}