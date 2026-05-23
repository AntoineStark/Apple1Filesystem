// fake6502.cpp – MOS 6502 emulator core
// Public domain. Based on Mike Chambers' fake6502 (2011).
#include "fake6502.h"
#include <cassert>
#include <cstring>

namespace fake6502 {

// ─── Global emulator state ────────────────────────────────────────────────────
static State g;
static ReadFn  g_read  = nullptr;
static WriteFn g_write = nullptr;

// Per-instruction temporaries
static uint16_t ea;         // effective address
static uint8_t  penaltyop;  // 1 if op reads (for page-cross penalty)
static uint8_t  penaltyaddr;// 1 if page was crossed

// ─── Memory helpers ───────────────────────────────────────────────────────────
static inline uint8_t  rd(uint16_t a)              { return g_read(a); }
static inline void     wr(uint16_t a, uint8_t v)   { g_write(a, v); }
static inline uint16_t rd16(uint16_t a)            { return rd(a) | (uint16_t(rd(a+1)) << 8); }
// 6502 page-wrap bug: $xxFF wraps to $xx00 for high byte
static inline uint16_t rd16bug(uint16_t a)         {
    uint16_t lo = rd(a);
    uint16_t hi = rd((a & 0xFF00) | ((a+1) & 0x00FF));
    return lo | (hi << 8);
}

// ─── Stack helpers ────────────────────────────────────────────────────────────
static inline void push(uint8_t v)  { wr(0x100 + g.sp--, v); }
static inline uint8_t pop()         { return rd(0x100 + ++g.sp); }
static inline void push16(uint16_t v){ push(v >> 8); push(v & 0xFF); }
static inline uint16_t pop16()      { uint16_t l = pop(); return l | (uint16_t(pop()) << 8); }

// ─── Flag helpers ─────────────────────────────────────────────────────────────
static inline void setZN(uint8_t v) {
    if (v == 0) g.status |= FLAG_ZERO;  else g.status &= ~FLAG_ZERO;
    if (v & 0x80) g.status |= FLAG_SIGN; else g.status &= ~FLAG_SIGN;
}

// ─── Addressing modes ─────────────────────────────────────────────────────────
// Each mode sets `ea` and advances PC as needed.
static void imp()  { /* ea unused */ }
// acc: accumulator mode – ea unused; ops use g.a directly (sentinel set by acc_sentinel)
static void imm()  { ea = g.pc++; }
static void zp()   { ea = rd(g.pc++); }
static void zpx()  { ea = (rd(g.pc++) + g.x) & 0xFF; }
static void zpy()  { ea = (rd(g.pc++) + g.y) & 0xFF; }
static void abso() { ea = rd16(g.pc); g.pc += 2; }
static void absx() {
    uint16_t base = rd16(g.pc); g.pc += 2;
    ea = base + g.x;
    if ((base & 0xFF00) != (ea & 0xFF00)) penaltyaddr = 1;
}
static void absy() {
    uint16_t base = rd16(g.pc); g.pc += 2;
    ea = base + g.y;
    if ((base & 0xFF00) != (ea & 0xFF00)) penaltyaddr = 1;
}
static void ind()  { ea = rd16bug(rd16(g.pc)); g.pc += 2; }
static void indx() { uint8_t t = rd(g.pc++) + g.x; ea = rd16bug(t & 0xFF); }
static void indy() {
    uint8_t t = rd(g.pc++);
    uint16_t base = rd16bug(t);
    ea = base + g.y;
    if ((base & 0xFF00) != (ea & 0xFF00)) penaltyaddr = 1;
}
static void rel()  {
    int8_t offset = (int8_t)rd(g.pc++);
    ea = g.pc + offset;
}

// ─── Operations ──────────────────────────────────────────────────────────────
#define GETVAL()  (penaltyop ? rd(ea) : rd(ea))
// (same but marks that this op needs the extra cycle if penaltyaddr)
#define GETVAL_PEN() ({ penaltyop=1; rd(ea); })

static void adc() {
    penaltyop = 1;
    uint16_t v = rd(ea);
    uint16_t res;
    if (g.status & FLAG_DECIMAL) {
        // BCD mode
        uint16_t lo = (g.a & 0x0F) + (v & 0x0F) + (g.status & FLAG_CARRY);
        if (lo > 9) lo += 6;
        uint16_t hi = (g.a >> 4) + (v >> 4) + (lo > 15 ? 1 : 0);
        if (hi > 9) hi += 6;
        res = (lo & 0x0F) | (hi << 4);
        if (res > 0xFF) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
    } else {
        res = g.a + v + (g.status & FLAG_CARRY);
        if (res > 0xFF) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
    }
    // overflow: sign changed when it shouldn't
    if (!((g.a ^ v) & 0x80) && ((g.a ^ res) & 0x80))
        g.status |= FLAG_OVERFLOW;
    else
        g.status &= ~FLAG_OVERFLOW;
    g.a = res & 0xFF;
    setZN(g.a);
}
static void and_() { penaltyop = 1; g.a &= rd(ea); setZN(g.a); }
static void asl() {
    if (ea == 0xFFFF) { // ACC mode sentinel – we use ea=0xFFFF for acc
        if (g.a & 0x80) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
        g.a <<= 1; setZN(g.a);
    } else {
        uint8_t v = rd(ea);
        if (v & 0x80) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
        v <<= 1; wr(ea, v); setZN(v);
    }
}
static void bcc() { if (!(g.status & FLAG_CARRY))    { g.pc = ea; } }
static void bcs() { if (  g.status & FLAG_CARRY)     { g.pc = ea; } }
static void beq() { if (  g.status & FLAG_ZERO)      { g.pc = ea; } }
static void bit() {
    uint8_t v = rd(ea);
    if (v & g.a) g.status &= ~FLAG_ZERO; else g.status |= FLAG_ZERO;
    g.status = (g.status & 0x3F) | (v & 0xC0);
}
static void bmi() { if (  g.status & FLAG_SIGN)      { g.pc = ea; } }
static void bne() { if (!(g.status & FLAG_ZERO))     { g.pc = ea; } }
static void bpl() { if (!(g.status & FLAG_SIGN))     { g.pc = ea; } }
static void brk() {
    g.pc++;
    push16(g.pc);
    push(g.status | FLAG_BREAK);
    g.status |= FLAG_INTERRUPT;
    g.pc = rd16(0xFFFE);
}
static void bvc() { if (!(g.status & FLAG_OVERFLOW)) { g.pc = ea; } }
static void bvs() { if (  g.status & FLAG_OVERFLOW)  { g.pc = ea; } }
static void clc() { g.status &= ~FLAG_CARRY; }
static void cld() { g.status &= ~FLAG_DECIMAL; }
static void cli() { g.status &= ~FLAG_INTERRUPT; }
static void clv() { g.status &= ~FLAG_OVERFLOW; }
static void cmp() {
    penaltyop = 1;
    uint16_t res = g.a - rd(ea);
    if (res < 0x100) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
    setZN(res & 0xFF);
}
static void cpx() {
    uint16_t res = g.x - rd(ea);
    if (res < 0x100) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
    setZN(res & 0xFF);
}
static void cpy() {
    uint16_t res = g.y - rd(ea);
    if (res < 0x100) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
    setZN(res & 0xFF);
}
static void dec() { uint8_t v = rd(ea) - 1; wr(ea, v); setZN(v); }
static void dex() { --g.x; setZN(g.x); }
static void dey() { --g.y; setZN(g.y); }
static void eor() { penaltyop = 1; g.a ^= rd(ea); setZN(g.a); }
static void inc() { uint8_t v = rd(ea) + 1; wr(ea, v); setZN(v); }
static void inx() { ++g.x; setZN(g.x); }
static void iny() { ++g.y; setZN(g.y); }
static void jmp() { g.pc = ea; }
static void jsr() { push16(g.pc - 1); g.pc = ea; }
static void lda() { penaltyop = 1; g.a = rd(ea); setZN(g.a); }
static void ldx() { penaltyop = 1; g.x = rd(ea); setZN(g.x); }
static void ldy() { penaltyop = 1; g.y = rd(ea); setZN(g.y); }
static void lsr() {
    if (ea == 0xFFFF) {
        if (g.a & 0x01) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
        g.a >>= 1; setZN(g.a);
    } else {
        uint8_t v = rd(ea);
        if (v & 0x01) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
        v >>= 1; wr(ea, v); setZN(v);
    }
}
static void nop() { penaltyop = 1; }
static void ora() { penaltyop = 1; g.a |= rd(ea); setZN(g.a); }
static void pha() { push(g.a); }
static void php() { push(g.status | FLAG_BREAK | FLAG_CONSTANT); }
static void pla() { g.a = pop(); setZN(g.a); }
static void plp() { g.status = (pop() & ~FLAG_BREAK) | FLAG_CONSTANT; }
static void rol() {
    uint8_t cin = g.status & FLAG_CARRY;
    if (ea == 0xFFFF) {
        if (g.a & 0x80) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
        g.a = (g.a << 1) | cin; setZN(g.a);
    } else {
        uint8_t v = rd(ea);
        if (v & 0x80) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
        v = (v << 1) | cin; wr(ea, v); setZN(v);
    }
}
static void ror() {
    uint8_t cin = (g.status & FLAG_CARRY) << 7;
    if (ea == 0xFFFF) {
        if (g.a & 0x01) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
        g.a = (g.a >> 1) | cin; setZN(g.a);
    } else {
        uint8_t v = rd(ea);
        if (v & 0x01) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
        v = (v >> 1) | cin; wr(ea, v); setZN(v);
    }
}
static void rti() {
    g.status = (pop() & ~FLAG_BREAK) | FLAG_CONSTANT;
    g.pc = pop16();
}
static void rts() { g.pc = pop16() + 1; }
static void sbc() {
    penaltyop = 1;
    uint16_t v = rd(ea) ^ 0xFF; // one's complement for subtraction
    uint16_t res;
    if (g.status & FLAG_DECIMAL) {
        uint16_t lo = (g.a & 0x0F) - (rd(ea) & 0x0F) + (g.status & FLAG_CARRY) - 1;
        uint16_t hi = (g.a >> 4) - (rd(ea) >> 4);
        if (lo & 0x10) { lo -= 6; hi--; }
        if (hi & 0x10) hi -= 6;
        res = (lo & 0x0F) | (hi << 4);
        // carry is set if no borrow
        uint16_t full = g.a + v + (g.status & FLAG_CARRY);
        if (full > 0xFF) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
        if (!((g.a ^ v) & 0x80) && ((g.a ^ full) & 0x80))
            g.status |= FLAG_OVERFLOW;
        else
            g.status &= ~FLAG_OVERFLOW;
        g.a = res & 0xFF;
    } else {
        res = g.a + v + (g.status & FLAG_CARRY);
        if (res > 0xFF) g.status |= FLAG_CARRY; else g.status &= ~FLAG_CARRY;
        if (!((g.a ^ v) & 0x80) && ((g.a ^ res) & 0x80))
            g.status |= FLAG_OVERFLOW;
        else
            g.status &= ~FLAG_OVERFLOW;
        g.a = res & 0xFF;
    }
    setZN(g.a);
}
static void sec() { g.status |= FLAG_CARRY; }
static void sed() { g.status |= FLAG_DECIMAL; }
static void sei() { g.status |= FLAG_INTERRUPT; }
static void sta() { wr(ea, g.a); }
static void stx() { wr(ea, g.x); }
static void sty() { wr(ea, g.y); }
static void tax() { g.x = g.a; setZN(g.x); }
static void tay() { g.y = g.a; setZN(g.y); }
static void tsx() { g.x = g.sp; setZN(g.x); }
static void txa() { g.a = g.x; setZN(g.a); }
static void txs() { g.sp = g.x; }
static void tya() { g.a = g.y; setZN(g.a); }
static void kil() { g.stopped = true; } // undocumented KIL/JAM

// Illegal/undocumented NOPs – same as nop

// ─── Instruction table ────────────────────────────────────────────────────────
// Each entry: { addr_mode_fn, op_fn, base_cycles }
struct Instr { void(*mode)(); void(*op)(); uint8_t cycles; };

// We use 0xFFFF as the "accumulator" sentinel for ea in ASL/LSR/ROL/ROR
static void acc_sentinel() { ea = 0xFFFF; }

static const Instr table[256] = {
//       0               1               2               3
/* 00 */ {imp,  brk, 7},{indx, ora, 6},{imp,  kil, 2},{indx, nop, 8},
/* 04 */ {zp,   nop, 3},{zp,   ora, 3},{zp,   asl, 5},{zp,   nop, 5},
/* 08 */ {imp,  php, 3},{imm,  ora, 2},{acc_sentinel,asl,2},{imm,nop,2},
/* 0C */ {abso, nop, 4},{abso, ora, 4},{abso, asl, 6},{abso, nop, 6},
/* 10 */ {rel,  bpl, 2},{indy, ora, 5},{imp,  kil, 2},{indy, nop, 8},
/* 14 */ {zpx,  nop, 4},{zpx,  ora, 4},{zpx,  asl, 6},{zpx,  nop, 6},
/* 18 */ {imp,  clc, 2},{absy, ora, 4},{imp,  nop, 2},{absy, nop, 7},
/* 1C */ {absx, nop, 4},{absx, ora, 4},{absx, asl, 7},{absx, nop, 7},
/* 20 */ {abso, jsr, 6},{indx, and_,6},{imp,  kil, 2},{indx, nop, 8},
/* 24 */ {zp,   bit, 3},{zp,   and_,3},{zp,   rol, 5},{zp,   nop, 5},
/* 28 */ {imp,  plp, 4},{imm,  and_,2},{acc_sentinel,rol,2},{imm,nop,2},
/* 2C */ {abso, bit, 4},{abso, and_,4},{abso, rol, 6},{abso, nop, 6},
/* 30 */ {rel,  bmi, 2},{indy, and_,5},{imp,  kil, 2},{indy, nop, 8},
/* 34 */ {zpx,  nop, 4},{zpx,  and_,4},{zpx,  rol, 6},{zpx,  nop, 6},
/* 38 */ {imp,  sec, 2},{absy, and_,4},{imp,  nop, 2},{absy, nop, 7},
/* 3C */ {absx, nop, 4},{absx, and_,4},{absx, rol, 7},{absx, nop, 7},
/* 40 */ {imp,  rti, 6},{indx, eor, 6},{imp,  kil, 2},{indx, nop, 8},
/* 44 */ {zp,   nop, 3},{zp,   eor, 3},{zp,   lsr, 5},{zp,   nop, 5},
/* 48 */ {imp,  pha, 3},{imm,  eor, 2},{acc_sentinel,lsr,2},{imm,nop,2},
/* 4C */ {abso, jmp, 3},{abso, eor, 4},{abso, lsr, 6},{abso, nop, 6},
/* 50 */ {rel,  bvc, 2},{indy, eor, 5},{imp,  kil, 2},{indy, nop, 8},
/* 54 */ {zpx,  nop, 4},{zpx,  eor, 4},{zpx,  lsr, 6},{zpx,  nop, 6},
/* 58 */ {imp,  cli, 2},{absy, eor, 4},{imp,  nop, 2},{absy, nop, 7},
/* 5C */ {absx, nop, 4},{absx, eor, 4},{absx, lsr, 7},{absx, nop, 7},
/* 60 */ {imp,  rts, 6},{indx, adc, 6},{imp,  kil, 2},{indx, nop, 8},
/* 64 */ {zp,   nop, 3},{zp,   adc, 3},{zp,   ror, 5},{zp,   nop, 5},
/* 68 */ {imp,  pla, 4},{imm,  adc, 2},{acc_sentinel,ror,2},{imm,nop,2},
/* 6C */ {ind,  jmp, 5},{abso, adc, 4},{abso, ror, 6},{abso, nop, 6},
/* 70 */ {rel,  bvs, 2},{indy, adc, 5},{imp,  kil, 2},{indy, nop, 8},
/* 74 */ {zpx,  nop, 4},{zpx,  adc, 4},{zpx,  ror, 6},{zpx,  nop, 6},
/* 78 */ {imp,  sei, 2},{absy, adc, 4},{imp,  nop, 2},{absy, nop, 7},
/* 7C */ {absx, nop, 4},{absx, adc, 4},{absx, ror, 7},{absx, nop, 7},
/* 80 */ {imm,  nop, 2},{indx, sta, 6},{imm,  nop, 2},{indx, nop, 6},
/* 84 */ {zp,   sty, 3},{zp,   sta, 3},{zp,   stx, 3},{zp,   nop, 3},
/* 88 */ {imp,  dey, 2},{imm,  nop, 2},{imp,  txa, 2},{imm,  nop, 2},
/* 8C */ {abso, sty, 4},{abso, sta, 4},{abso, stx, 4},{abso, nop, 4},
/* 90 */ {rel,  bcc, 2},{indy, sta, 6},{imp,  kil, 2},{indy, nop, 6},
/* 94 */ {zpx,  sty, 4},{zpx,  sta, 4},{zpy,  stx, 4},{zpy,  nop, 4},
/* 98 */ {imp,  tya, 2},{absy, sta, 5},{imp,  txs, 2},{absy, nop, 5},
/* 9C */ {absx, nop, 5},{absx, sta, 5},{absy, nop, 5},{absy, nop, 5},
/* A0 */ {imm,  ldy, 2},{indx, lda, 6},{imm,  ldx, 2},{indx, nop, 6},
/* A4 */ {zp,   ldy, 3},{zp,   lda, 3},{zp,   ldx, 3},{zp,   nop, 3},
/* A8 */ {imp,  tay, 2},{imm,  lda, 2},{imp,  tax, 2},{imm,  nop, 2},
/* AC */ {abso, ldy, 4},{abso, lda, 4},{abso, ldx, 4},{abso, nop, 4},
/* B0 */ {rel,  bcs, 2},{indy, lda, 5},{imp,  kil, 2},{indy, nop, 5},
/* B4 */ {zpx,  ldy, 4},{zpx,  lda, 4},{zpy,  ldx, 4},{zpy,  nop, 4},
/* B8 */ {imp,  clv, 2},{absy, lda, 4},{imp,  tsx, 2},{absy, nop, 4},
/* BC */ {absx, ldy, 4},{absx, lda, 4},{absy, ldx, 4},{absy, nop, 4},
/* C0 */ {imm,  cpy, 2},{indx, cmp, 6},{imm,  nop, 2},{indx, nop, 8},
/* C4 */ {zp,   cpy, 3},{zp,   cmp, 3},{zp,   dec, 5},{zp,   nop, 5},
/* C8 */ {imp,  iny, 2},{imm,  cmp, 2},{imp,  dex, 2},{imm,  nop, 2},
/* CC */ {abso, cpy, 4},{abso, cmp, 4},{abso, dec, 6},{abso, nop, 6},
/* D0 */ {rel,  bne, 2},{indy, cmp, 5},{imp,  kil, 2},{indy, nop, 8},
/* D4 */ {zpx,  nop, 4},{zpx,  cmp, 4},{zpx,  dec, 6},{zpx,  nop, 6},
/* D8 */ {imp,  cld, 2},{absy, cmp, 4},{imp,  nop, 2},{absy, nop, 7},
/* DC */ {absx, nop, 4},{absx, cmp, 4},{absx, dec, 7},{absx, nop, 7},
/* E0 */ {imm,  cpx, 2},{indx, sbc, 6},{imm,  nop, 2},{indx, nop, 8},
/* E4 */ {zp,   cpx, 3},{zp,   sbc, 3},{zp,   inc, 5},{zp,   nop, 5},
/* E8 */ {imp,  inx, 2},{imm,  sbc, 2},{imp,  nop, 2},{imm,  nop, 2},
/* EC */ {abso, cpx, 4},{abso, sbc, 4},{abso, inc, 6},{abso, nop, 6},
/* F0 */ {rel,  beq, 2},{indy, sbc, 5},{imp,  kil, 2},{indy, nop, 8},
/* F4 */ {zpx,  nop, 4},{zpx,  sbc, 4},{zpx,  inc, 6},{zpx,  nop, 6},
/* F8 */ {imp,  sed, 2},{absy, sbc, 4},{imp,  nop, 2},{absy, nop, 7},
/* FC */ {absx, nop, 4},{absx, sbc, 4},{absx, inc, 7},{absx, nop, 7},
};

// ─── Branch penalty helper ───────────────────────────────────────────────────
// Called after a branch op to add 1 cycle if taken, 2 if page crossed
static int branch_penalty(uint16_t old_pc) {
    if (g.pc == old_pc) return 0;          // not taken
    if ((g.pc & 0xFF00) != (old_pc & 0xFF00)) return 2;
    return 1;
}

// ─── Public API ──────────────────────────────────────────────────────────────
void set_callbacks(ReadFn r, WriteFn w) { g_read = r; g_write = w; }

void reset() {
    g = State{};
    g.pc = rd16(0xFFFC);
    g.sp = 0xFD;
    g.status = FLAG_CONSTANT | FLAG_INTERRUPT;
    g.clockticks = 0;
    g.stopped = false;
}

void nmi() {
    push16(g.pc);
    push(g.status & ~FLAG_BREAK);
    g.status |= FLAG_INTERRUPT;
    g.pc = rd16(0xFFFA);
    g.clockticks += 7;
}

void irq() {
    if (g.status & FLAG_INTERRUPT) return;
    push16(g.pc);
    push(g.status & ~FLAG_BREAK);
    g.status |= FLAG_INTERRUPT;
    g.pc = rd16(0xFFFE);
    g.clockticks += 7;
}

int step() {
    if (g.stopped) return 1;

    penaltyop = 0; penaltyaddr = 0;
    uint8_t opcode = rd(g.pc++);
    const Instr& ins = table[opcode];

    uint16_t pc_before_branch = g.pc; // used for branch penalty

    ins.mode();
    ins.op();

    int cycles = ins.cycles;

    // Branch ops: bcc bcs beq bmi bne bpl bvc bvs — add penalty
    // Branch opcodes: 10,30,50,70,90,B0,D0,F0
    switch (opcode) {
        case 0x10: case 0x30: case 0x50: case 0x70:
        case 0x90: case 0xB0: case 0xD0: case 0xF0:
            cycles += branch_penalty(pc_before_branch);
            break;
        default:
            if (penaltyop && penaltyaddr) cycles++;
            break;
    }

    g.clockticks += cycles;
    return cycles;
}

void exec(uint32_t ticks) {
    uint32_t target = g.clockticks + ticks;
    while (g.clockticks < target && !g.stopped)
        step();
}

const State& state() { return g; }

void restore_state(const State& s) { g = s; }

} // namespace fake6502
