#include "apple1/machine.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    void check(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct Fixture
    {
        apple1::Machine machine;
        Fixture()
        {
            const auto image = apple1::Machine::read_binary(APPLE1_RAMFS_IMAGE);
            check(image.size() == 4096, "filesystem routine ROM size");
            std::copy(image.begin(), image.end(), machine.rom.begin() + 0x2000);
            machine.rom[0x7ffc] = 0;
            machine.rom[0x7ffd] = 0x20;
            machine.reset();
            machine.cpu.write = [this](uint16_t address, uint8_t value)
            {
                check((address >= 0x40 && address <= 0x4b) ||
                          (address >= 0x100 && address < 0x200) ||
                          (address >= 0x500 && address < 0x5f0) ||
                          (address >= 0xa000 && address < 0xb000),
                      "guest write escaped filesystem, scratch, stack, or output buffer");
                machine.write(address, value);
            };
        }
        uint8_t call(unsigned operation, uint8_t argument = 0)
        {
            machine.cpu.pc = static_cast<uint16_t>(0x2000 + operation * 3);
            machine.cpu.a = argument;
            machine.cpu.p |= 8;
            machine.cpu.sp = 0xfd;
            machine.write(0x1fe, 0xff);
            machine.write(0x1ff, 0x05);
            const auto original = machine.ram;
            const auto started = machine.ticks();
            while (machine.cpu.pc != 0x0600 && machine.ticks() - started < 2000000)
                machine.step();
            check(machine.cpu.pc == 0x0600, "routine must return within cycle budget");
            check(std::equal(original.begin() + 0x3000, original.end(), machine.ram.begin() + 0x3000),
                  "routine wrote beyond filesystem region");
            check(std::equal(original.begin() + 0x1000, original.begin() + 0x2000,
                             machine.ram.begin() + 0x1000),
                  "routine wrote unrelated low RAM");
            check((machine.cpu.p & 1) == (machine.cpu.a != 0), "status carry agrees with error code");
            return machine.cpu.a;
        }
        void name(const std::string &text)
        {
            machine.write(0x40, 0);
            machine.write(0x41, 3);
            for (size_t index = 0; index <= text.size(); ++index)
                machine.write(static_cast<uint16_t>(0x300 + index),
                              index == text.size() ? 0 : text[index]);
        }
        uint8_t put(const std::string &filename, const std::vector<uint8_t> &bytes)
        {
            name(filename);
            machine.write(0x42, 0);
            machine.write(0x43, 4);
            machine.write(0x44, static_cast<uint8_t>(bytes.size()));
            for (size_t index = 0; index < bytes.size(); ++index)
                machine.write(static_cast<uint16_t>(0x400 + index), bytes[index]);
            return call(3);
        }
        std::vector<uint8_t> get(const std::string &filename)
        {
            name(filename);
            machine.write(0x42, 0);
            machine.write(0x43, 5);
            check(call(4) == 0, "read " + filename);
            std::vector<uint8_t> bytes(machine.peek(0x44));
            for (size_t index = 0; index < bytes.size(); ++index)
                bytes[index] = machine.peek(static_cast<uint16_t>(0x500 + index));
            return bytes;
        }
    };

    void roundtrip()
    {
        Fixture fixture;
        check(fixture.call(0) == 5, "unformatted RAM rejected");
        check(fixture.call(1) == 0 && fixture.call(0) == 0, "format and mount");
        std::vector<uint8_t> content(240);
        for (size_t index = 0; index < content.size(); ++index)
            content[index] = index;
        check(fixture.put("ABCDEFGH", content) == 0, "maximum name and size");
        check(fixture.get("ABCDEFGH") == content, "binary round-trip including zero bytes");
        check(fixture.put("EMPTY", {}) == 0 && fixture.get("EMPTY").empty(), "empty file");
        check(fixture.put("ABCDEFGH", {42}) == 0 && fixture.get("ABCDEFGH") == std::vector<uint8_t>{42},
              "overwrite and truncate");
        check(fixture.call(6, 0) == 0 && fixture.machine.peek(0x44) == 1, "enumerate slot");
        check(fixture.call(6, 15) == 6, "enumeration bounds");
        check(fixture.put("BAD NAME", {1}) == 3, "invalid name");
        check(fixture.put("TOOLONG99", {1}) == 3, "long name");
        check(fixture.put("", {1}) == 3, "empty name");
        check(fixture.put("BIG", std::vector<uint8_t>(241)) == 4, "oversize rejected");
        check(fixture.put("SMALL", {0}) == 0 && fixture.get("SMALL") == std::vector<uint8_t>{0},
              "one-byte binary file");
        fixture.name("MISSING");
        check(fixture.call(2) == 1 && fixture.call(5) == 1, "missing lookup and delete");
        fixture.machine.reset();
        check(fixture.call(0) == 0 && fixture.get("ABCDEFGH") == std::vector<uint8_t>{42},
              "reset preserves files");
        fixture.machine.power_cycle();
        check(fixture.call(0) == 5, "power cycle clears filesystem");
    }

    void full_and_reuse()
    {
        Fixture fixture;
        check(fixture.call(1) == 0, "format");
        for (unsigned index = 0; index < 15; ++index)
            check(fixture.put("F" + std::to_string(index), {static_cast<uint8_t>(index)}) == 0, "fill slots");
        const auto full = fixture.machine.ram;
        check(fixture.put("EXTRA", {99}) == 2, "directory full");
        check(std::equal(full.begin() + 0x2000, full.begin() + 0x3000,
                         fixture.machine.ram.begin() + 0x2000),
              "full failure preserves filesystem");
        check(fixture.put("F14", {88}) == 0, "overwrite when full");
        fixture.name("F5");
        check(fixture.call(5) == 0, "delete");
        check(fixture.put("REUSED", {77}) == 0, "reuse deleted slot");
        check(fixture.get("F14") == std::vector<uint8_t>{88}, "other files intact");
        check(fixture.call(0) == 0, "mount full filesystem");
    }

    void corruption()
    {
        for (const uint16_t address : {0xa000, 0xa004, 0xa108, 0xaf08})
        {
            Fixture fixture;
            fixture.call(1);
            for (unsigned index = 0; index < 15; ++index)
                check(fixture.put("F" + std::to_string(index), {1}) == 0, "prepare corruption test");
            fixture.machine.write(address, 255);
            const auto before = fixture.machine.ram;
            check(fixture.call(0) == 5, "reject corrupt header or length in any slot");
            fixture.name("F0");
            check(fixture.call(5) == 5, "refuse mutation on corrupt filesystem");
            check(std::equal(before.begin() + 0x2000, before.begin() + 0x3000,
                             fixture.machine.ram.begin() + 0x2000),
                  "mount never repairs destructively");
        }
        Fixture fixture;
        fixture.call(1);
        fixture.put("ONE", {1});
        fixture.put("TWO", {2});
        for (unsigned index = 0; index < 8; ++index)
            fixture.machine.write(0xa200 + index, fixture.machine.peek(0xa100 + index));
        check(fixture.call(0) == 5, "duplicate names rejected");
        fixture.call(1);
        fixture.put("ONE", {1});
        fixture.machine.write(0xa105, 'X');
        check(fixture.call(0) == 5, "nonzero name padding rejected");
        fixture.call(1);
        fixture.put("ONE", {1});
        fixture.machine.write(0xa100, '!');
        check(fixture.call(0) == 5, "invalid stored name rejected");
    }
}

int main()
{
    try
    {
        roundtrip();
        full_and_reuse();
        corruption();
        std::cout << "RAM filesystem guest-code tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}