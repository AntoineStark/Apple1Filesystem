#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// fake6502 – public-domain MOS 6502 emulator core
// Adapted from Mike Chambers' fake6502 (2011, public domain)
// ─────────────────────────────────────────────────────────────────────────────
// The host must supply two callbacks before calling any 6502 function:
//
//   uint8_t  mem_read (uint16_t addr);
//   void     mem_write(uint16_t addr, uint8_t val);
//
// Set them via fake6502_set_callbacks().
// ─────────────────────────────────────────────────────────────────────────────

namespace fake6502 {

// CPU register state – readable by the host
struct State {
    uint16_t pc{};
    uint8_t  sp{0xFD};
    uint8_t  a{}, x{}, y{};
    uint8_t  status{0x24};          // unused + interrupt bits set at reset
    uint32_t clockticks{};          // total cycles executed
    bool     stopped{false};        // set true on KIL/JAM
};

// Callback types
using ReadFn  = uint8_t  (*)(uint16_t addr);
using WriteFn = void     (*)(uint16_t addr, uint8_t val);

// Install memory callbacks (call before reset/step/exec)
void set_callbacks(ReadFn r, WriteFn w);

// CPU control
void reset();
void nmi();
void irq();

// Execute exactly one instruction; returns cycles consumed
int  step();

// Execute until at least `ticks` cycles have been run (from current clockticks)
void exec(uint32_t ticks);

// Access current state (read-only snapshot)
const State& state();

// Restore CPU registers from a previously saved state
// (callbacks are NOT changed; RAM must be restored separately)
void restore_state(const State& s);

// Status flag bit masks
constexpr uint8_t FLAG_CARRY     = 0x01;
constexpr uint8_t FLAG_ZERO      = 0x02;
constexpr uint8_t FLAG_INTERRUPT = 0x04;
constexpr uint8_t FLAG_DECIMAL   = 0x08;
constexpr uint8_t FLAG_BREAK     = 0x10;
constexpr uint8_t FLAG_CONSTANT  = 0x20;
constexpr uint8_t FLAG_OVERFLOW  = 0x40;
constexpr uint8_t FLAG_SIGN      = 0x80;

} // namespace fake6502
