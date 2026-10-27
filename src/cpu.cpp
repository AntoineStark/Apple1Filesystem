#include "apple1/cpu.hpp"

#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace apple1
{
    namespace
    {

        constexpr uint8_t C = 0x01, Z = 0x02, I = 0x04, D = 0x08;
        constexpr uint8_t B = 0x10, U = 0x20, V = 0x40, N = 0x80;

        enum class Mode
        {
            Imp,
            Acc,
            Imm,
            Zp,
            Zpx,
            Zpy,
            Abs,
            Abx,
            Aby,
            Ind,
            Izx,
            Izy,
            Rel
        };
        enum class Op
        {
            Invalid,
            ADC,
            AND,
            ASL,
            BCC,
            BCS,
            BEQ,
            BIT,
            BMI,
            BNE,
            BPL,
            BRK,
            BVC,
            BVS,
            CLC,
            CLD,
            CLI,
            CLV,
            CMP,
            CPX,
            CPY,
            DEC,
            DEX,
            DEY,
            EOR,
            INC,
            INX,
            INY,
            JMP,
            JSR,
            LDA,
            LDX,
            LDY,
            LSR,
            NOP,
            ORA,
            PHA,
            PHP,
            PLA,
            PLP,
            ROL,
            ROR,
            RTI,
            RTS,
            SBC,
            SEC,
            SED,
            SEI,
            STA,
            STX,
            STY,
            TAX,
            TAY,
            TSX,
            TXA,
            TXS,
            TYA
        };

        struct Instruction
        {
            const char *name = nullptr;
            Op op = Op::Invalid;
            Mode mode = Mode::Imp;
            unsigned cycles = 0;
            bool page = false;
        };

        const std::array<Instruction, 256> instructions = []
        {
            std::array<Instruction, 256> table{};
            auto add = [&](unsigned code, const char *name, Op op, Mode mode,
                           unsigned cycles, bool page = false)
            {
                table[code] = {name, op, mode, cycles, page};
            };
#define INS(code, name, mode, cycles, ...) \
    add(0x##code, #name, Op::name, Mode::mode, cycles __VA_OPT__(, ) __VA_ARGS__)
            INS(00, BRK, Imp, 7);
            INS(01, ORA, Izx, 6);
            INS(05, ORA, Zp, 3);
            INS(06, ASL, Zp, 5);
            INS(08, PHP, Imp, 3);
            INS(09, ORA, Imm, 2);
            INS(0A, ASL, Acc, 2);
            INS(0D, ORA, Abs, 4);
            INS(0E, ASL, Abs, 6);
            INS(10, BPL, Rel, 2);
            INS(11, ORA, Izy, 5, true);
            INS(15, ORA, Zpx, 4);
            INS(16, ASL, Zpx, 6);
            INS(18, CLC, Imp, 2);
            INS(19, ORA, Aby, 4, true);
            INS(1D, ORA, Abx, 4, true);
            INS(1E, ASL, Abx, 7);
            INS(20, JSR, Abs, 6);
            INS(21, AND, Izx, 6);
            INS(24, BIT, Zp, 3);
            INS(25, AND, Zp, 3);
            INS(26, ROL, Zp, 5);
            INS(28, PLP, Imp, 4);
            INS(29, AND, Imm, 2);
            INS(2A, ROL, Acc, 2);
            INS(2C, BIT, Abs, 4);
            INS(2D, AND, Abs, 4);
            INS(2E, ROL, Abs, 6);
            INS(30, BMI, Rel, 2);
            INS(31, AND, Izy, 5, true);
            INS(35, AND, Zpx, 4);
            INS(36, ROL, Zpx, 6);
            INS(38, SEC, Imp, 2);
            INS(39, AND, Aby, 4, true);
            INS(3D, AND, Abx, 4, true);
            INS(3E, ROL, Abx, 7);
            INS(40, RTI, Imp, 6);
            INS(41, EOR, Izx, 6);
            INS(45, EOR, Zp, 3);
            INS(46, LSR, Zp, 5);
            INS(48, PHA, Imp, 3);
            INS(49, EOR, Imm, 2);
            INS(4A, LSR, Acc, 2);
            INS(4C, JMP, Abs, 3);
            INS(4D, EOR, Abs, 4);
            INS(4E, LSR, Abs, 6);
            INS(50, BVC, Rel, 2);
            INS(51, EOR, Izy, 5, true);
            INS(55, EOR, Zpx, 4);
            INS(56, LSR, Zpx, 6);
            INS(58, CLI, Imp, 2);
            INS(59, EOR, Aby, 4, true);
            INS(5D, EOR, Abx, 4, true);
            INS(5E, LSR, Abx, 7);
            INS(60, RTS, Imp, 6);
            INS(61, ADC, Izx, 6);
            INS(65, ADC, Zp, 3);
            INS(66, ROR, Zp, 5);
            INS(68, PLA, Imp, 4);
            INS(69, ADC, Imm, 2);
            INS(6A, ROR, Acc, 2);
            INS(6C, JMP, Ind, 5);
            INS(6D, ADC, Abs, 4);
            INS(6E, ROR, Abs, 6);
            INS(70, BVS, Rel, 2);
            INS(71, ADC, Izy, 5, true);
            INS(75, ADC, Zpx, 4);
            INS(76, ROR, Zpx, 6);
            INS(78, SEI, Imp, 2);
            INS(79, ADC, Aby, 4, true);
            INS(7D, ADC, Abx, 4, true);
            INS(7E, ROR, Abx, 7);
            INS(81, STA, Izx, 6);
            INS(84, STY, Zp, 3);
            INS(85, STA, Zp, 3);
            INS(86, STX, Zp, 3);
            INS(88, DEY, Imp, 2);
            INS(8A, TXA, Imp, 2);
            INS(8C, STY, Abs, 4);
            INS(8D, STA, Abs, 4);
            INS(8E, STX, Abs, 4);
            INS(90, BCC, Rel, 2);
            INS(91, STA, Izy, 6);
            INS(94, STY, Zpx, 4);
            INS(95, STA, Zpx, 4);
            INS(96, STX, Zpy, 4);
            INS(98, TYA, Imp, 2);
            INS(99, STA, Aby, 5);
            INS(9A, TXS, Imp, 2);
            INS(9D, STA, Abx, 5);
            INS(A0, LDY, Imm, 2);
            INS(A1, LDA, Izx, 6);
            INS(A2, LDX, Imm, 2);
            INS(A4, LDY, Zp, 3);
            INS(A5, LDA, Zp, 3);
            INS(A6, LDX, Zp, 3);
            INS(A8, TAY, Imp, 2);
            INS(A9, LDA, Imm, 2);
            INS(AA, TAX, Imp, 2);
            INS(AC, LDY, Abs, 4);
            INS(AD, LDA, Abs, 4);
            INS(AE, LDX, Abs, 4);
            INS(B0, BCS, Rel, 2);
            INS(B1, LDA, Izy, 5, true);
            INS(B4, LDY, Zpx, 4);
            INS(B5, LDA, Zpx, 4);
            INS(B6, LDX, Zpy, 4);
            INS(B8, CLV, Imp, 2);
            INS(B9, LDA, Aby, 4, true);
            INS(BA, TSX, Imp, 2);
            INS(BC, LDY, Abx, 4, true);
            INS(BD, LDA, Abx, 4, true);
            INS(BE, LDX, Aby, 4, true);
            INS(C0, CPY, Imm, 2);
            INS(C1, CMP, Izx, 6);
            INS(C4, CPY, Zp, 3);
            INS(C5, CMP, Zp, 3);
            INS(C6, DEC, Zp, 5);
            INS(C8, INY, Imp, 2);
            INS(C9, CMP, Imm, 2);
            INS(CA, DEX, Imp, 2);
            INS(CC, CPY, Abs, 4);
            INS(CD, CMP, Abs, 4);
            INS(CE, DEC, Abs, 6);
            INS(D0, BNE, Rel, 2);
            INS(D1, CMP, Izy, 5, true);
            INS(D5, CMP, Zpx, 4);
            INS(D6, DEC, Zpx, 6);
            INS(D8, CLD, Imp, 2);
            INS(D9, CMP, Aby, 4, true);
            INS(DD, CMP, Abx, 4, true);
            INS(DE, DEC, Abx, 7);
            INS(E0, CPX, Imm, 2);
            INS(E1, SBC, Izx, 6);
            INS(E4, CPX, Zp, 3);
            INS(E5, SBC, Zp, 3);
            INS(E6, INC, Zp, 5);
            INS(E8, INX, Imp, 2);
            INS(E9, SBC, Imm, 2);
            INS(EA, NOP, Imp, 2);
            INS(EC, CPX, Abs, 4);
            INS(ED, SBC, Abs, 4);
            INS(EE, INC, Abs, 6);
            INS(F0, BEQ, Rel, 2);
            INS(F1, SBC, Izy, 5, true);
            INS(F5, SBC, Zpx, 4);
            INS(F6, INC, Zpx, 6);
            INS(F8, SED, Imp, 2);
            INS(F9, SBC, Aby, 4, true);
            INS(FD, SBC, Abx, 4, true);
            INS(FE, INC, Abx, 7);
#undef INS
            return table;
        }();

        std::string hex(unsigned value, unsigned width)
        {
            std::ostringstream stream;
            stream << '$' << std::uppercase << std::hex << std::setfill('0')
                   << std::setw(static_cast<int>(width)) << value;
            return stream.str();
        }

    }

    void Cpu::reset()
    {
        a = x = y = 0;
        sp = 0xfd;
        p = I | U;
        irq = nmi = false;
        pc = static_cast<uint16_t>(read(0xfffc) | (uint16_t(read(0xfffd)) << 8));
        cycles = 7;
    }

    unsigned Cpu::step()
    {
        auto flag = [&](uint8_t mask, bool value)
        {
            p = static_cast<uint8_t>((p & ~mask) | (value ? mask : 0));
        };
        auto nz = [&](uint8_t value)
        {
            flag(Z, value == 0);
            flag(N, (value & N) != 0);
        };
        auto push = [&](uint8_t value)
        { write(0x100 | sp, value); --sp; };
        auto pull = [&]()
        { ++sp; return read(0x100 | sp); };
        auto vector = [&](uint16_t address)
        {
            const uint8_t low = read(address);
            return static_cast<uint16_t>(low | (uint16_t(read(uint16_t(address + 1))) << 8));
        };
        auto interrupt = [&](uint16_t address, bool software)
        {
            push(static_cast<uint8_t>(pc >> 8));
            push(static_cast<uint8_t>(pc));
            push(static_cast<uint8_t>((p | U) & ~B) | (software ? B : 0));
            p = static_cast<uint8_t>((p | I | U) & ~B);
            pc = vector(address);
        };
        if (nmi || (irq && !(p & I)))
        {
            const bool serviceNmi = nmi;
            if (serviceNmi)
                nmi = false;
            interrupt(serviceNmi ? 0xfffa : 0xfffe, false);
            cycles += 7;
            return 7;
        }
        const uint16_t start = pc;
        const uint8_t opcode = read(pc);
        const Instruction &instruction = instructions[opcode];
        if (instruction.op == Op::Invalid)
        {
            throw std::runtime_error("Unsupported opcode " + hex(opcode, 2) +
                                     " at PC " + hex(start, 4));
        }
        ++pc;
        p = static_cast<uint8_t>((p | U) & ~B);
        auto fetch = [&]()
        { const uint8_t value = read(pc); ++pc; return value; };
        auto word = [&]()
        {
            const uint8_t low = fetch();
            return static_cast<uint16_t>(low | (uint16_t(fetch()) << 8));
        };
        uint16_t address = 0;
        bool crossed = false;
        auto indexed = [&](uint16_t base, uint8_t index)
        {
            address = static_cast<uint16_t>(base + index);
            crossed = (base & 0xff00) != (address & 0xff00);
        };
        switch (instruction.mode)
        {
        case Mode::Imp:
        case Mode::Acc:
            break;
        case Mode::Imm:
            address = pc++;
            break;
        case Mode::Zp:
            address = fetch();
            break;
        case Mode::Zpx:
            address = static_cast<uint8_t>(fetch() + x);
            break;
        case Mode::Zpy:
            address = static_cast<uint8_t>(fetch() + y);
            break;
        case Mode::Abs:
            address = word();
            break;
        case Mode::Abx:
            indexed(word(), x);
            break;
        case Mode::Aby:
            indexed(word(), y);
            break;
        case Mode::Ind:
        {
            const uint16_t pointer = word();
            const uint8_t low = read(pointer);
            address = static_cast<uint16_t>(low | (uint16_t(read(
                                                       (pointer & 0xff00) | static_cast<uint8_t>(pointer + 1)))
                                                   << 8));
            break;
        }
        case Mode::Izx:
        {
            const uint8_t pointer = static_cast<uint8_t>(fetch() + x);
            const uint8_t low = read(pointer);
            address = static_cast<uint16_t>(low | (uint16_t(read(uint8_t(pointer + 1))) << 8));
            break;
        }
        case Mode::Izy:
        {
            const uint8_t pointer = fetch();
            const uint8_t low = read(pointer);
            indexed(static_cast<uint16_t>(low | (uint16_t(read(uint8_t(pointer + 1))) << 8)), y);
            break;
        }
        case Mode::Rel:
        {
            const uint8_t offset = fetch();
            const int displacement = offset < 0x80 ? int(offset) : int(offset) - 256;
            address = static_cast<uint16_t>(pc + displacement);
            crossed = (pc & 0xff00) != (address & 0xff00);
            break;
        }
        }
        unsigned elapsed = instruction.cycles + (instruction.page && crossed ? 1 : 0);
        auto branch = [&](bool taken)
        {
            if (taken)
            {
                pc = address;
                elapsed += crossed ? 2 : 1;
            }
        };
        auto compare = [&](uint8_t reg)
        {
            const uint8_t value = read(address);
            flag(C, reg >= value);
            nz(static_cast<uint8_t>(reg - value));
        };
        auto shift = [&](Op op)
        {
            uint8_t value = instruction.mode == Mode::Acc ? a : read(address);
            const bool carry = (p & C) != 0;
            if (op == Op::ASL || op == Op::ROL)
            {
                flag(C, (value & 0x80) != 0);
                value = static_cast<uint8_t>((value << 1) | (op == Op::ROL && carry ? 1 : 0));
            }
            else
            {
                flag(C, (value & 1) != 0);
                value = static_cast<uint8_t>((value >> 1) | (op == Op::ROR && carry ? 0x80 : 0));
            }
            nz(value);
            if (instruction.mode == Mode::Acc)
                a = value;
            else
                write(address, value);
        };
        switch (instruction.op)
        {
        case Op::ADC:
        {
            const uint8_t value = read(address);
            const unsigned carry = p & C;
            const unsigned binary = unsigned(a) + value + carry;
            if (p & D)
            {
                unsigned low = (a & 15) + (value & 15) + carry;
                if (low >= 10)
                    low = ((low + 6) & 15) + 16;
                unsigned adjusted = (a & 0xf0) + (value & 0xf0) + low;
                flag(Z, static_cast<uint8_t>(binary) == 0);
                flag(N, (adjusted & 0x80) != 0);
                flag(V, (~(a ^ value) & (a ^ adjusted) & 0x80) != 0);
                if (adjusted >= 0xa0)
                    adjusted += 0x60;
                flag(C, adjusted > 0xff);
                a = static_cast<uint8_t>(adjusted);
            }
            else
            {
                flag(V, (~(a ^ value) & (a ^ binary) & 0x80) != 0);
                flag(C, binary > 0xff);
                a = static_cast<uint8_t>(binary);
                nz(a);
            }
            break;
        }
        case Op::SBC:
        {
            const uint8_t value = read(address);
            const int borrow = (p & C) ? 0 : 1;
            const int binary = int(a) - value - borrow;
            flag(V, ((a ^ value) & (a ^ binary) & 0x80) != 0);
            flag(C, binary >= 0);
            nz(static_cast<uint8_t>(binary));
            if (p & D)
            {
                int low = (a & 15) - (value & 15) - borrow;
                if (low < 0)
                    low = ((low - 6) & 15) - 16;
                int adjusted = (a & 0xf0) - (value & 0xf0) + low;
                if (adjusted < 0)
                    adjusted -= 0x60;
                a = static_cast<uint8_t>(adjusted);
            }
            else
                a = static_cast<uint8_t>(binary);
            break;
        }
        case Op::AND:
            a &= read(address);
            nz(a);
            break;
        case Op::ORA:
            a |= read(address);
            nz(a);
            break;
        case Op::EOR:
            a ^= read(address);
            nz(a);
            break;
        case Op::ASL:
        case Op::LSR:
        case Op::ROL:
        case Op::ROR:
            shift(instruction.op);
            break;
        case Op::BCC:
            branch(!(p & C));
            break;
        case Op::BCS:
            branch(p & C);
            break;
        case Op::BEQ:
            branch(p & Z);
            break;
        case Op::BMI:
            branch(p & N);
            break;
        case Op::BNE:
            branch(!(p & Z));
            break;
        case Op::BPL:
            branch(!(p & N));
            break;
        case Op::BVC:
            branch(!(p & V));
            break;
        case Op::BVS:
            branch(p & V);
            break;
        case Op::BIT:
        {
            const uint8_t value = read(address);
            flag(Z, (a & value) == 0);
            p = static_cast<uint8_t>((p & ~(N | V)) | (value & (N | V)));
            break;
        }
        case Op::BRK:
            ++pc;
            interrupt(0xfffe, true);
            break;
        case Op::CLC:
            flag(C, false);
            break;
        case Op::CLD:
            flag(D, false);
            break;
        case Op::CLI:
            flag(I, false);
            break;
        case Op::CLV:
            flag(V, false);
            break;
        case Op::SEC:
            flag(C, true);
            break;
        case Op::SED:
            flag(D, true);
            break;
        case Op::SEI:
            flag(I, true);
            break;
        case Op::CMP:
            compare(a);
            break;
        case Op::CPX:
            compare(x);
            break;
        case Op::CPY:
            compare(y);
            break;
        case Op::DEC:
        case Op::INC:
        {
            const uint8_t value = static_cast<uint8_t>(read(address) + (instruction.op == Op::INC ? 1 : -1));
            write(address, value);
            nz(value);
            break;
        }
        case Op::DEX:
            --x;
            nz(x);
            break;
        case Op::DEY:
            --y;
            nz(y);
            break;
        case Op::INX:
            ++x;
            nz(x);
            break;
        case Op::INY:
            ++y;
            nz(y);
            break;
        case Op::JMP:
            pc = address;
            break;
        case Op::JSR:
        {
            const uint16_t returnAddress = static_cast<uint16_t>(pc - 1);
            push(static_cast<uint8_t>(returnAddress >> 8));
            push(static_cast<uint8_t>(returnAddress));
            pc = address;
            break;
        }
        case Op::LDA:
            a = read(address);
            nz(a);
            break;
        case Op::LDX:
            x = read(address);
            nz(x);
            break;
        case Op::LDY:
            y = read(address);
            nz(y);
            break;
        case Op::NOP:
            break;
        case Op::PHA:
            push(a);
            break;
        case Op::PHP:
            push(p | B | U);
            break;
        case Op::PLA:
            a = pull();
            nz(a);
            break;
        case Op::PLP:
            p = static_cast<uint8_t>((pull() | U) & ~B);
            break;
        case Op::RTI:
        {
            p = static_cast<uint8_t>((pull() | U) & ~B);
            const uint8_t low = pull();
            pc = static_cast<uint16_t>(low | (uint16_t(pull()) << 8));
            break;
        }
        case Op::RTS:
        {
            const uint8_t low = pull();
            pc = static_cast<uint16_t>((low | (uint16_t(pull()) << 8)) + 1);
            break;
        }
        case Op::STA:
            write(address, a);
            break;
        case Op::STX:
            write(address, x);
            break;
        case Op::STY:
            write(address, y);
            break;
        case Op::TAX:
            x = a;
            nz(x);
            break;
        case Op::TAY:
            y = a;
            nz(y);
            break;
        case Op::TSX:
            x = sp;
            nz(x);
            break;
        case Op::TXA:
            a = x;
            nz(a);
            break;
        case Op::TXS:
            sp = x;
            break;
        case Op::TYA:
            a = y;
            nz(a);
            break;
        case Op::Invalid:
            throw std::logic_error("Invalid instruction table entry");
        }
        cycles += elapsed;
        return elapsed;
    }

    std::string Cpu::disassemble(uint16_t address, const std::function<uint8_t(uint16_t)> &peek)
    {
        const uint8_t opcode = peek(address++);
        const Instruction &instruction = instructions[opcode];
        if (instruction.op == Op::Invalid)
            return ".BYTE " + hex(opcode, 2);
        std::string result = instruction.name;
        if (instruction.mode == Mode::Imp)
            return result;
        if (instruction.mode == Mode::Acc)
            return result + " A";
        const uint8_t low = peek(address++);
        auto absolute = [&]()
        {
            return hex(low | (unsigned(peek(address)) << 8), 4);
        };
        switch (instruction.mode)
        {
        case Mode::Imm:
            return result + " #" + hex(low, 2);
        case Mode::Zp:
            return result + " " + hex(low, 2);
        case Mode::Zpx:
            return result + " " + hex(low, 2) + ",X";
        case Mode::Zpy:
            return result + " " + hex(low, 2) + ",Y";
        case Mode::Abs:
            return result + " " + absolute();
        case Mode::Abx:
            return result + " " + absolute() + ",X";
        case Mode::Aby:
            return result + " " + absolute() + ",Y";
        case Mode::Ind:
            return result + " (" + absolute() + ")";
        case Mode::Izx:
            return result + " (" + hex(low, 2) + ",X)";
        case Mode::Izy:
            return result + " (" + hex(low, 2) + "),Y";
        case Mode::Rel:
            return result + " " + hex(static_cast<uint16_t>(address + (low < 0x80 ? int(low) : int(low) - 256)), 4);
        case Mode::Imp:
        case Mode::Acc:
            break;
        }
        throw std::logic_error("Invalid addressing mode");
    }

}