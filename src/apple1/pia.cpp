#include "pia.h"
#include <cctype>

// ─── Keyboard ────────────────────────────────────────────────────────────────

void PIA::enqueue_key(char c) {
    // Apple 1 keyboard: uppercase ASCII only, bit 7 set
    // CR (0x0D) is the return key; DEL ($7F) is backspace
    uint8_t k = (uint8_t)c & 0x7F; // strip bit 7
    if (k >= 'a' && k <= 'z') k -= 32; // force uppercase
    kbd_queue.push_back(k | 0x80);
}

uint8_t PIA::kbd_read() {
    if (!kbd_queue.empty()) {
        kbd_latch = kbd_queue.front();
        kbd_queue.pop_front();
        kbd_ready = false;
    }
    return kbd_latch | 0x80;
}

uint8_t PIA::kbdcr_read() {
    // Bit 7: key ready
    if (!kbd_queue.empty()) kbd_ready = true;
    return kbd_ready ? 0x80 : 0x00;
}

// ─── Display ─────────────────────────────────────────────────────────────────

uint8_t PIA::dsp_read() {
    // Bit 7 = busy (1 = display not ready for next char)
    return dsp_busy ? 0x80 : 0x00;
}

void PIA::dsp_write(uint8_t val) {
    // CPU writes char | 0x80 to DSP
    uint8_t c = val & 0x7F;
    if (output_char && c) {
        output_char((char)c);
    }
    // Mark busy briefly – real Apple 1 display takes ~16ms per character
    // (the 6821 PIA charges/discharges a capacitor for ~16ms before clearing busy).
    // At 1 MHz CPU: 16ms ≈ 16,000 cycles.  The loader uses this timing to detect
    // that the screen is actually present; too few cycles → loader falls back to Wozmon.
    dsp_busy = true;
    dsp_busy_cycles = 16000;
}

void PIA::dspcr_write(uint8_t /*val*/) {
    // Control register write – ignored in basic emulation
}

// ─── Tick ────────────────────────────────────────────────────────────────────

void PIA::tick(int cycles) {
    if (dsp_busy) {
        dsp_busy_cycles -= cycles;
        if (dsp_busy_cycles <= 0) {
            dsp_busy = false;
            dsp_busy_cycles = 0;
        }
    }
}
