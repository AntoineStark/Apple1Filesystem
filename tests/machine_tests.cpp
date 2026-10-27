#include "apple1/machine.hpp"
#include <iostream>
#include <stdexcept>

using namespace apple1;

static void check(bool result, const char *message)
{
    if (!result)
        throw std::runtime_error(message);
}

int main()
{
    try
    {
        Machine machine;
        machine.write(0, 0x42);
        check(machine.peek(0) == 0x42, "card RAM");
        machine.segments[8] = Segment::Ram;
        check(machine.peek(0x8000) == 0x42, "RAM alias");
        machine.write(0x8000, 0x17);
        check(machine.peek(0) == 0x17, "shared alias writes");
        machine.rom[0x7000] = 0x66;
        check(machine.peek(0x7000) == 0x66 && machine.peek(0xf000) == 0x66, "ROM alias");
        machine.write(0xf000, 0);
        check(machine.peek(0xf000) == 0x66, "ROM write protection");
        machine.motherboard_low = true;
        bool conflict = false;
        try
        {
            machine.validate_mapping();
        }
        catch (const std::runtime_error &)
        {
            conflict = true;
        }
        check(conflict, "mapping conflict detection");
        machine.motherboard_low = false;
        machine.write(0xd012, 0x7f);
        machine.write(0xd013, 0xa7);
        machine.write(0xd011, 0xa7);
        machine.write(0xd012, 'A');
        check((machine.peek(0xd012) & 0x80) != 0, "display busy");
        machine.enqueue("a");
        machine.ram[0x200] = 0x4c;
        machine.ram[0x201] = 0;
        machine.ram[0x202] = 2;
        machine.cpu.pc = 0x200;
        for (int count = 0; count < 6000; ++count)
            machine.step();
        check(machine.screen[0][0] == 'A', "timed display output");
        check((machine.peek(0xd011) & 0x80) != 0, "keyboard ready");
        check(machine.peek(0xd010) == 0xc1, "uppercase high-bit keyboard");
        check((machine.peek(0xd011) & 0x80) != 0, "peek has no side effects");
        check(machine.read(0xd010) == 0xc1 && !(machine.peek(0xd011) & 0x80), "keyboard acknowledge");
        machine.enqueue("buffered text");
        for (int count = 0; count < 400; ++count)
            machine.step();
        check(machine.pending_input() > 0 && (machine.peek(0xd011) & 0x80), "input includes latched and queued bytes");
        machine.clear_input();
        check(machine.pending_input() == 0 && !(machine.peek(0xd011) & 0x80), "clear input empties queue and latch");
        machine.reset();
        check(machine.peek(0) == 0x17, "reset preserves RAM");
        Machine standard(Profile::Standard);
        standard.validate_mapping();
        standard.write(0xe000, 0x33);
        check(standard.peek(0xe000) == 0x33 && standard.peek(0x1000) == 0xff, "standard RAM map");
        std::cout << "Machine mapping and PIA tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}