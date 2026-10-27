#include "apple1/cpu.hpp"

#include <array>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

    void check(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct Machine
    {
        std::array<uint8_t, 65536> memory{};
        apple1::Cpu cpu;
        Machine()
        {
            cpu.read = [this](uint16_t address)
            { return memory[address]; };
            cpu.write = [this](uint16_t address, uint8_t value)
            { memory[address] = value; };
            cpu.pc = 0x200;
        }
        void code(std::initializer_list<uint8_t> bytes, uint16_t address = 0x200)
        {
            cpu.pc = address;
            for (uint8_t value : bytes)
                memory[address++] = value;
        }
        void tick(unsigned expected)
        {
            const uint64_t before = cpu.cycles;
            check(cpu.step() == expected, "step cycle count");
            check(cpu.cycles == before + expected, "accumulated cycles");
        }
    };

    void arithmetic()
    {
        Machine machine;
        auto &cpu = machine.cpu;
        for (unsigned opcode : {0x69u, 0xe9u})
        {
            for (unsigned left = 0; left < 256; ++left)
            {
                for (unsigned right = 0; right < 256; ++right)
                {
                    for (unsigned carry = 0; carry < 2; ++carry)
                    {
                        machine.code({static_cast<uint8_t>(opcode), static_cast<uint8_t>(right)});
                        cpu.a = static_cast<uint8_t>(left);
                        cpu.p = static_cast<uint8_t>(0x24 | carry);
                        machine.tick(2);
                        const int result = opcode == 0x69 ? int(left + right + carry)
                                                          : int(left) - int(right) - int(1 - carry);
                        const uint8_t value = static_cast<uint8_t>(result);
                        const bool overflow = opcode == 0x69
                                                  ? (~(left ^ right) & (left ^ value) & 0x80) != 0
                                                  : ((left ^ right) & (left ^ value) & 0x80) != 0;
                        const bool carryOut = opcode == 0x69 ? result > 255 : result >= 0;
                        const unsigned flags = (value & 0x80) | (overflow ? 0x40 : 0) |
                                               (value == 0 ? 2 : 0) | unsigned(carryOut);
                        check(cpu.a == value && cpu.p == (0x24 | flags), "exhaustive binary ADC/SBC");
                    }
                }
            }
        }
        for (unsigned left = 0; left < 100; ++left)
        {
            for (unsigned right = 0; right < 100; ++right)
            {
                for (unsigned carry = 0; carry < 2; ++carry)
                {
                    const unsigned packedLeft = (left / 10) * 16 + left % 10;
                    const unsigned packedRight = (right / 10) * 16 + right % 10;
                    for (unsigned opcode : {0x69u, 0xe9u})
                    {
                        machine.code({static_cast<uint8_t>(opcode), static_cast<uint8_t>(packedRight)});
                        cpu.a = static_cast<uint8_t>(packedLeft);
                        cpu.p = static_cast<uint8_t>(0x2c | carry);
                        machine.tick(2);
                        const bool addition = opcode == 0x69;
                        const int decimal = addition ? int(left + right + carry)
                                                     : int(left) - int(right) - int(1 - carry);
                        const unsigned wrapped = static_cast<unsigned>((decimal + 100) % 100);
                        check(cpu.a == (wrapped / 10) * 16 + wrapped % 10, "BCD result");
                        const int binary = addition ? int(packedLeft + packedRight + carry)
                                                    : int(packedLeft) - int(packedRight) - int(1 - carry);
                        const unsigned lowSum = (packedLeft & 15) + (packedRight & 15) + carry;
                        const unsigned intermediate = (packedLeft & 0xf0) + (packedRight & 0xf0) +
                                                      (lowSum >= 10 ? (lowSum + 6) % 16 + 16 : lowSum);
                        const unsigned flagResult = addition ? intermediate : static_cast<unsigned>(binary);
                        const bool overflow = addition
                                                  ? (~(packedLeft ^ packedRight) & (packedLeft ^ flagResult) & 0x80) != 0
                                                  : ((packedLeft ^ packedRight) & (packedLeft ^ flagResult) & 0x80) != 0;
                        const unsigned flags = (flagResult & 0x80) | (overflow ? 0x40 : 0) |
                                               (static_cast<uint8_t>(binary) == 0 ? 2 : 0) |
                                               unsigned(addition ? decimal >= 100 : decimal >= 0);
                        check(cpu.p == (0x2c | flags), "NMOS decimal flags");
                    }
                }
            }
        }
        machine.code({0x69, 0x50});
        cpu.a = 0x50;
        cpu.p = 0x28;
        machine.tick(2);
        check(cpu.a == 0 && (cpu.p & 0xc3) == 0xc1, "NMOS decimal zero is not adjusted-result zero");
        machine.code({0x69, 0x0f});
        cpu.a = 0x0f;
        cpu.p = 0x28;
        machine.tick(2);
        check(cpu.a == 0x14, "invalid BCD ADC nibble correction");
        machine.code({0xe9, 0x0f});
        cpu.a = 0;
        cpu.p = 0x29;
        machine.tick(2);
        check(cpu.a == 0x9b, "invalid BCD SBC nibble correction");
    }

    void addressingAndCycles()
    {
        Machine machine;
        auto &cpu = machine.cpu;
        machine.code({0xb5, 0xff});
        cpu.x = 2;
        machine.memory[1] = 0x81;
        machine.tick(4);
        check(cpu.a == 0x81, "zero-page X wrap");
        machine.code({0xb6, 0xff});
        cpu.y = 2;
        machine.tick(4);
        check(cpu.x == 0x81, "zero-page Y wrap");
        machine.code({0xa1, 0xfe});
        cpu.x = 1;
        machine.memory[0xff] = 0x34;
        machine.memory[0] = 0x12;
        machine.memory[0x1234] = 0x55;
        machine.tick(6);
        check(cpu.a == 0x55, "indexed indirect pointer wrap");
        machine.code({0xb1, 0xff});
        cpu.y = 1;
        machine.memory[0xff] = 0xff;
        machine.memory[0] = 0x12;
        machine.memory[0x1300] = 0x66;
        machine.tick(6);
        check(cpu.a == 0x66, "indirect indexed pointer wrap and page penalty");
        cpu.y = 0;
        cpu.pc = 0x200;
        machine.tick(5);
        machine.code({0xbd, 0xff, 0xff});
        cpu.x = 1;
        machine.memory[0] = 0x77;
        machine.tick(5);
        check(cpu.a == 0x77, "absolute indexed 16-bit wrap");
        machine.code({0xbd, 0x00, 0x12});
        machine.tick(4);
        machine.code({0x99, 0xff, 0x12});
        cpu.y = 1;
        cpu.a = 0x33;
        machine.tick(5);
        check(machine.memory[0x1300] == 0x33, "store fixed cycles");
        machine.code({0xfe, 0xff, 0x12});
        cpu.x = 1;
        machine.tick(7);
        check(machine.memory[0x1300] == 0x34, "RMW fixed cycles");
        machine.code({0x6c, 0xff, 0x12});
        machine.memory[0x12ff] = 0x78;
        machine.memory[0x1200] = 0x56;
        machine.memory[0x1300] = 0x99;
        machine.tick(5);
        check(cpu.pc == 0x5678, "NMOS indirect JMP page bug");
        machine.code({0xa9, 0x42}, 0xffff);
        machine.tick(2);
        check(cpu.a == 0x42 && cpu.pc == 1, "instruction fetch wrap");
        const std::array<uint8_t, 8> branches{0x10, 0x30, 0x50, 0x70, 0x90, 0xb0, 0xd0, 0xf0};
        const std::array<uint8_t, 8> masks{0x80, 0x80, 0x40, 0x40, 1, 1, 2, 2};
        for (unsigned index = 0; index < branches.size(); ++index)
        {
            const uint8_t taken = index % 2 ? masks[index] : 0;
            machine.code({branches[index], 0x02});
            cpu.p = 0x24 | taken;
            machine.tick(3);
            check(cpu.pc == 0x204, "taken branch");
            machine.code({branches[index], 0x02});
            cpu.p = 0x24 | (taken ^ masks[index]);
            machine.tick(2);
            check(cpu.pc == 0x202, "untaken branch");
            machine.code({branches[index], 0xfe}, 0x20fe);
            cpu.p = 0x24 | taken;
            machine.tick(4);
            check(cpu.pc == 0x20fe, "backward branch crossing page");
        }
        machine.code({0xd0, 0xfe}, 0xffff);
        cpu.p = 0x24;
        machine.tick(4);
        check(cpu.pc == 0xffff, "branch address wrap");
    }

    void stackAndInterrupts()
    {
        Machine machine;
        auto &cpu = machine.cpu;
        machine.memory[0xfffc] = 0x34;
        machine.memory[0xfffd] = 0x12;
        cpu.a = 5;
        cpu.irq = cpu.nmi = true;
        cpu.cycles = 99;
        cpu.reset();
        check(cpu.pc == 0x1234 && cpu.sp == 0xfd && cpu.p == 0x24 && cpu.cycles == 7 &&
                  cpu.a == 0 && cpu.x == 0 && cpu.y == 0 && !cpu.irq && !cpu.nmi,
              "cold reset");
        machine.code({0x48, 0x68});
        cpu.sp = 0;
        cpu.a = 0x80;
        machine.tick(3);
        check(cpu.sp == 0xff && machine.memory[0x100] == 0x80, "push wraps SP");
        cpu.a = 0;
        machine.tick(4);
        check(cpu.a == 0x80 && cpu.sp == 0 && (cpu.p & 0x82) == 0x80, "pull wraps SP and sets NZ");
        machine.code({0x08, 0x28});
        cpu.p = 0x69;
        machine.tick(3);
        check(machine.memory[0x100] == 0x79, "PHP stack-image bits");
        machine.memory[0x100] = 0xdf;
        machine.tick(4);
        check(cpu.p == 0xef, "PLP ignores B and forces U");
        machine.code({0x20, 0x00, 0x30});
        cpu.sp = 0;
        machine.memory[0x3000] = 0x60;
        machine.tick(6);
        check(cpu.pc == 0x3000 && cpu.sp == 0xfe && machine.memory[0x100] == 2 &&
                  machine.memory[0x1ff] == 2,
              "JSR return address and stack wrap");
        machine.tick(6);
        check(cpu.pc == 0x203 && cpu.sp == 0, "RTS");
        machine.memory[0xfffe] = 0;
        machine.memory[0xffff] = 0x40;
        machine.memory[0xfffa] = 0;
        machine.memory[0xfffb] = 0x50;
        machine.memory[0x4000] = machine.memory[0x5000] = 0x40;
        machine.code({0x00, 0xea});
        cpu.sp = 0xfd;
        cpu.p = 0x29;
        machine.tick(7);
        check(cpu.pc == 0x4000 && machine.memory[0x1fd] == 2 && machine.memory[0x1fc] == 2 &&
                  machine.memory[0x1fb] == 0x39 && cpu.p == 0x2d,
              "BRK stack, D preservation, and vector");
        machine.tick(6);
        check(cpu.pc == 0x202 && cpu.p == 0x29, "RTI restores status and PC");
        cpu.irq = true;
        machine.tick(7);
        check(cpu.pc == 0x4000 && machine.memory[0x1fb] == 0x29 && cpu.irq, "IRQ pushes B clear and remains asserted");
        cpu.nmi = true;
        machine.tick(7);
        check(cpu.pc == 0x5000 && !cpu.nmi && cpu.sp == 0xf7, "NMI overrides I and consumes latch");
        machine.tick(6);
        machine.tick(6);
        check(cpu.pc == 0x202 && cpu.p == 0x29, "nested interrupt RTI");
        machine.tick(7);
        check(cpu.pc == 0x4000, "level IRQ retriggers");
        cpu.irq = false;
        machine.tick(6);
        machine.code({0xea});
        cpu.p = 0x24;
        cpu.irq = true;
        machine.tick(2);
        check(cpu.pc == 0x201, "I masks IRQ");
        cpu.nmi = true;
        machine.tick(7);
        check(cpu.pc == 0x5000, "NMI wins simultaneous IRQ");
    }

    void operations()
    {
        Machine machine;
        auto &cpu = machine.cpu;
        machine.code({0xa9, 0x80, 0xaa, 0xa8, 0x8a, 0x98, 0x9a, 0xba});
        for (unsigned index = 0; index < 7; ++index)
            machine.tick(2);
        check(cpu.a == 0x80 && cpu.x == 0x80 && cpu.y == 0x80 && cpu.sp == 0x80, "transfers");
        machine.code({0xa2, 0xff, 0xe8, 0xca, 0xa0, 0xff, 0xc8, 0x88});
        machine.tick(2);
        machine.tick(2);
        check(cpu.x == 0 && (cpu.p & 2), "INX zero wrap");
        machine.tick(2);
        check(cpu.x == 0xff && (cpu.p & 0x80), "DEX negative wrap");
        machine.tick(2);
        machine.tick(2);
        check(cpu.y == 0 && (cpu.p & 2), "INY zero wrap");
        machine.tick(2);
        check(cpu.y == 0xff && (cpu.p & 0x80), "DEY negative wrap");
        machine.code({0x29, 0x0f, 0x09, 0x80, 0x49, 0x8f});
        cpu.a = 0xff;
        machine.tick(2);
        check(cpu.a == 15, "AND");
        machine.tick(2);
        check(cpu.a == 0x8f && (cpu.p & 0x80), "ORA");
        machine.tick(2);
        check(cpu.a == 0 && (cpu.p & 2), "EOR");
        machine.code({0x24, 0x10});
        machine.memory[0x10] = 0xc0;
        cpu.a = 1;
        cpu.p = 0x25;
        machine.tick(3);
        check(cpu.p == 0xe7, "BIT NZV and carry preservation");
        for (uint8_t opcode : std::array<uint8_t, 3>{0xc9, 0xe0, 0xc0})
        {
            cpu.a = cpu.x = cpu.y = 0x40;
            machine.code({opcode, 0x40});
            machine.tick(2);
            check((cpu.p & 0x83) == 3, "compare equal");
            machine.code({opcode, 0x41});
            machine.tick(2);
            check((cpu.p & 0x83) == 0x80, "compare less");
            machine.code({opcode, 0x01});
            machine.tick(2);
            check((cpu.p & 0x83) == 1, "compare greater");
        }
        const std::array<uint8_t, 4> shifts{0x0a, 0x4a, 0x2a, 0x6a};
        const std::array<uint8_t, 4> results{0x02, 0x40, 0x03, 0xc0};
        for (unsigned index = 0; index < shifts.size(); ++index)
        {
            machine.code({shifts[index]});
            cpu.a = 0x81;
            cpu.p = 0x25;
            machine.tick(2);
            check(cpu.a == results[index] && (cpu.p & 1), "accumulator shift/rotate");
            machine.code({static_cast<uint8_t>(shifts[index] - 4), 0x10});
            machine.memory[0x10] = 0x81;
            cpu.p = 0x25;
            machine.tick(5);
            check(machine.memory[0x10] == results[index], "memory shift/rotate");
        }
        machine.code({0xe6, 0x10, 0xc6, 0x10});
        machine.memory[0x10] = 0xff;
        machine.tick(5);
        check(machine.memory[0x10] == 0 && (cpu.p & 2), "INC wrap");
        machine.tick(5);
        check(machine.memory[0x10] == 0xff && (cpu.p & 0x80), "DEC wrap");
        machine.code({0x84, 0x10, 0x86, 0x11, 0x85, 0x12});
        cpu.a = 1;
        cpu.x = 2;
        cpu.y = 3;
        machine.tick(3);
        machine.tick(3);
        machine.tick(3);
        check(machine.memory[0x10] == 3 && machine.memory[0x11] == 2 && machine.memory[0x12] == 1, "stores");
        machine.code({0x38, 0xf8, 0x78, 0x18, 0xd8, 0x58, 0xb8});
        cpu.p = 0x60;
        machine.tick(2);
        check(cpu.p & 1, "SEC");
        machine.tick(2);
        check(cpu.p & 8, "SED");
        machine.tick(2);
        check(cpu.p & 4, "SEI");
        machine.tick(2);
        machine.tick(2);
        machine.tick(2);
        machine.tick(2);
        check(cpu.p == 0x20, "clear flag instructions");
    }

    void decodeAndUnsupported()
    {
        Machine machine;
        const std::array<std::string, 16> legalRows{
            "00 01 05 06 08 09 0A 0D 0E", "10 11 15 16 18 19 1D 1E",
            "20 21 24 25 26 28 29 2A 2C 2D 2E", "30 31 35 36 38 39 3D 3E",
            "40 41 45 46 48 49 4A 4C 4D 4E", "50 51 55 56 58 59 5D 5E",
            "60 61 65 66 68 69 6A 6C 6D 6E", "70 71 75 76 78 79 7D 7E",
            "81 84 85 86 88 8A 8C 8D 8E", "90 91 94 95 96 98 99 9A 9D",
            "A0 A1 A2 A4 A5 A6 A8 A9 AA AC AD AE", "B0 B1 B4 B5 B6 B8 B9 BA BC BD BE",
            "C0 C1 C4 C5 C6 C8 C9 CA CC CD CE", "D0 D1 D5 D6 D8 D9 DD DE",
            "E0 E1 E4 E5 E6 E8 E9 EA EC ED EE", "F0 F1 F5 F6 F8 F9 FD FE"};
        unsigned legalCount = 0;
        for (unsigned opcode = 0; opcode < 256; ++opcode)
        {
            const std::string digits = "0123456789ABCDEF";
            const std::string token{digits[opcode >> 4], digits[opcode & 15]};
            const bool legal = legalRows[opcode >> 4].find(token) != std::string::npos;
            machine.memory.fill(0);
            machine.code({static_cast<uint8_t>(opcode), 0, 0});
            machine.cpu.p = 0x24;
            const uint64_t before = machine.cpu.cycles;
            bool threw = false;
            try
            {
                machine.cpu.step();
            }
            catch (const std::runtime_error &error)
            {
                threw = true;
                check(std::string(error.what()).find("$" + token) != std::string::npos &&
                          std::string(error.what()).find("$0200") != std::string::npos,
                      "illegal opcode diagnostic");
            }
            check(threw != legal, "exact documented opcode set");
            const std::string text = apple1::Cpu::disassemble(0x200, machine.cpu.read);
            check((text.find(".BYTE") == 0) != legal, "disassembly opcode set");
            if (legal)
                ++legalCount;
            else
                check(machine.cpu.pc == 0x200 && machine.cpu.cycles == before, "illegal opcode is nonmutating");
        }
        check(legalCount == 151, "151 documented opcodes");
        const std::array<uint8_t, 13> opcodes{0xea, 0x0a, 0xa9, 0xa5, 0xb5, 0xb6, 0xad, 0xbd, 0xb9, 0x6c, 0xa1, 0xb1, 0xd0};
        const std::array<std::string, 13> expected{
            "NOP", "ASL A", "LDA #$34", "LDA $34", "LDA $34,X", "LDX $34,Y",
            "LDA $1234", "LDA $1234,X", "LDA $1234,Y", "JMP ($1234)",
            "LDA ($34,X)", "LDA ($34),Y", "BNE $0236"};
        const std::array<unsigned, 13> lengths{1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2};
        for (unsigned index = 0; index < opcodes.size(); ++index)
        {
            machine.code({opcodes[index], 0x34, 0x12});
            unsigned reads = 0;
            const auto peek = [&](uint16_t address)
            { ++reads; return machine.memory[address]; };
            check(apple1::Cpu::disassemble(0x200, peek) == expected[index], "disassembly operand formatting");
            check(reads == lengths[index], "disassembly reads exactly instruction length");
        }
        machine.code({0xad, 0x34, 0x12}, 0xffff);
        check(apple1::Cpu::disassemble(0xffff, machine.cpu.read) == "LDA $1234", "disassembly fetch wrap");
        machine.code({0xd0, 0xfe}, 0xffff);
        check(apple1::Cpu::disassemble(0xffff, machine.cpu.read) == "BNE $FFFF", "disassembly relative wrap");
    }

    void functionalBinary(const char *path)
    {
        Machine machine;
        std::ifstream input(path, std::ios::binary);
        check(bool(input), "open functional binary");
        input.read(reinterpret_cast<char *>(machine.memory.data()), machine.memory.size());
        check(input.gcount() == 65536, "functional binary must be a full 64K image");
        machine.cpu.pc = 0x0400;
        for (uint64_t count = 0; count < 100000000; ++count)
        {
            const uint16_t before = machine.cpu.pc;
            machine.cpu.step();
            if (machine.cpu.pc == before)
            {
                check(before == 0x3469, "functional binary trapped at " + std::to_string(before));
                std::cout << "Klaus functional binary passed after " << count + 1 << " instructions\n";
                return;
            }
        }
        throw std::runtime_error("functional binary instruction limit exceeded");
    }

}

int main(int argc, char **argv)
{
    try
    {
        arithmetic();
        addressingAndCycles();
        stackAndInterrupts();
        operations();
        decodeAndUnsupported();
        if (argc == 2)
            functionalBinary(argv[1]);
        std::cout << "All CPU tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "CPU test failure: " << error.what() << '\n';
        return 1;
    }
}