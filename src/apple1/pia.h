#pragma once
#include <cstdint>
#include <functional>
#include <deque>

// ─────────────────────────────────────────────────────────────────────────────
// PIA – emulates the MC6821 dual PIA used in the Apple 1
//
// Port A: keyboard   (KBD=0xD010, KBDCR=0xD011)
// Port B: display    (DSP=0xD012, DSPCR=0xD013)
//
// Key protocol:
//   – Host calls enqueue_key(ascii) to deliver a keypress
//   – CPU polls KBDCR bit7: 1 = key waiting
//   – CPU reads KBD: returns char | 0x80, clears ready flag
//
// Display protocol:
//   – CPU writes char | 0x80 to DSP
//   – DSP bit7 reads as 1 (busy) briefly, then 0 (ready)
//   – We deliver character to output_char callback and clear busy immediately
//     (real hardware was slow; emulation can be instant)
// ─────────────────────────────────────────────────────────────────────────────

class PIA {
public:
    // Called by the frontend to deliver typed characters
    void enqueue_key(char c);

    // Callback: emulator sends display output here
    std::function<void(char)> output_char;

    // Bus read/write handlers – attach these to Bus callbacks
    uint8_t kbd_read();
    uint8_t kbdcr_read();
    uint8_t dsp_read();
    void    dsp_write(uint8_t val);
    void    dspcr_write(uint8_t val);

    // Tick: call every N CPU cycles if you want timed busy clearing (optional)
    void tick(int cycles);

private:
    std::deque<uint8_t> kbd_queue{};
    bool kbd_ready{false};
    uint8_t kbd_latch{0};

    bool dsp_busy{false};
    int  dsp_busy_cycles{0};   // countdown before clearing busy
};
