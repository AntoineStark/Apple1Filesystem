#include "apple1/machine.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    void check(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    struct Fixture
    {
        apple1::Machine machine;
        Fixture()
        {
            const auto image = apple1::Machine::read_binary(APPLE1_RAMFS_IMAGE);
            std::copy(image.begin(), image.end(), machine.rom.begin() + 0x2000);
            machine.rom[0x7ffc] = 0;
            machine.rom[0x7ffd] = 0x20;
            machine.reset();
            machine.cpu.write = [this](uint16_t address, uint8_t value)
            {
                check((address >= 0x40 && address <= 0x56) ||
                          (address >= 0x100 && address < 0x200) ||
                          (address >= 0x500 && address < 0x600) ||
                          (address >= 0xa00 && address <= 0xa48) ||
                          (address >= 0xa000 && address < 0xd000),
                      "filesystem write boundary");
                machine.write(address, value);
            };
            check(call(1) == 0, "format");
        }
        uint8_t call(unsigned operation, uint8_t argument = 0)
        {
            machine.cpu.pc = 0x2000 + operation * 3;
            machine.cpu.a = argument;
            machine.cpu.p |= 8;
            machine.cpu.sp = 0xfd;
            machine.write(0x1fe, 0xff);
            machine.write(0x1ff, 5);
            const auto began = machine.ticks();
            while (machine.cpu.pc != 0x600 && machine.ticks() - began < 10000000)
                machine.step();
            check(machine.cpu.pc == 0x600, "routine timeout operation " + std::to_string(operation));
            check((machine.cpu.p & 1) == (machine.cpu.a != 0), "carry error contract");
            return machine.cpu.a;
        }
        void name(const std::string &path)
        {
            machine.write(0x40, 0);
            machine.write(0x41, 3);
            for (size_t index = 0; index <= path.size(); ++index)
                machine.write(0x300 + index, index == path.size() ? 0 : path[index]);
        }
        uint8_t named(unsigned operation, const std::string &path)
        {
            name(path);
            return call(operation);
        }
        uint8_t put(const std::string &path, const std::vector<uint8_t> &data)
        {
            name(path);
            machine.write(0x42, 0);
            machine.write(0x43, 4);
            machine.write(0x44, data.size());
            for (size_t index = 0; index < data.size(); ++index)
                machine.write(0x400 + index, data[index]);
            return call(3);
        }
        std::vector<uint8_t> get(const std::string &path)
        {
            name(path);
            machine.write(0x42, 0);
            machine.write(0x43, 5);
            check(call(4) == 0, "get " + path);
            std::vector<uint8_t> result(machine.peek(0x44));
            for (size_t index = 0; index < result.size(); ++index)
                result[index] = machine.peek(0x500 + index);
            return result;
        }
        std::string pwd()
        {
            machine.write(0x42, 0);
            machine.write(0x43, 5);
            check(call(10) == 0, "pwd");
            std::string result;
            for (unsigned index = 0; index < machine.peek(0x44); ++index)
                result += char(machine.peek(0x500 + index));
            return result;
        }
    };
    void directories()
    {
        Fixture fixture;
        check(fixture.machine.peek(0xa004) == 2 && fixture.machine.peek(0xa011) == 2, "v2 root");
        check(fixture.pwd() == "/", "root pwd");
        check(fixture.named(7, "DOCS") == 0, "mkdir");
        check(fixture.named(7, "DOCS") == 10, "duplicate mkdir");
        check(fixture.named(7, "DOCS/SUB") == 0, "nested mkdir");
        check(fixture.put("/DOCS/NOTE", {1, 0, 3}) == 0, "absolute write");
        check(fixture.put("NOTE", {9}) == 0, "same name in root");
        check(fixture.named(9, "DOCS/SUB") == 0, "cd nested");
        check(fixture.pwd() == "/DOCS/SUB", "nested pwd");
        check(fixture.get("../NOTE") == std::vector<uint8_t>({1, 0, 3}), "parent relative read");
        check(fixture.get("/NOTE") == std::vector<uint8_t>({9}), "absolute from subdirectory");
        check(fixture.named(9, "./../SUB/..") == 0 && fixture.pwd() == "/DOCS", "dot components");
        check(fixture.named(8, "/DOCS") == 11, "cannot remove current directory");
        check(fixture.named(9, "/") == 0, "cd root");
        check(fixture.named(8, "DOCS") == 11, "cannot remove nonempty directory");
        check(fixture.named(5, "DOCS") == 9, "rm rejects directory");
        check(fixture.put("DOCS", {7}) == 9, "put rejects directory");
        check(fixture.named(9, "NOTE") == 9, "cd rejects file");
        check(fixture.named(8, "DOCS/SUB") == 0, "remove empty directory");
        check(fixture.named(5, "DOCS/NOTE") == 0 && fixture.named(8, "DOCS") == 0, "remove tree bottom up");
        check(fixture.put("MISSING/FILE", {1}) == 1, "missing parent");
        check(fixture.put("BAD!", {1}) == 3 && fixture.put("TOOLONG99", {1}) == 3, "bad component");
        std::vector<uint8_t> bytes(240);
        for (unsigned index = 0; index < bytes.size(); ++index)
            bytes[index] = index;
        check(fixture.put("ABCDEFGH", bytes) == 0 && fixture.get("ABCDEFGH") == bytes, "max component and binary data");
        check(fixture.put("ABCDEFGH", {}) == 0 && fixture.get("ABCDEFGH").empty(), "truncate to empty");
        check(fixture.put("BIG", std::vector<uint8_t>(241)) == 4, "oversize");
        fixture.machine.reset();
        fixture.machine.write(0x4c, 1);
        check(fixture.call(0) == 0 && fixture.get("NOTE") == std::vector<uint8_t>({9}), "reset retention");
        fixture.machine.power_cycle();
        fixture.machine.write(0x4c, 1);
        check(fixture.call(0) == 5, "power cycle clears");
    }
    void capacity()
    {
        Fixture fixture;
        for (unsigned directory = 0; directory < 3; ++directory)
        {
            const auto name = "D" + std::to_string(directory);
            check(fixture.named(7, name) == 0, "allocate directory");
            for (unsigned file = 0; file < 14; ++file)
                check(fixture.put(name + "/F" + std::to_string(file), {uint8_t(file)}) == 0, "allocate across 12 KiB");
        }
        check(fixture.put("LAST", {99}) == 0, "last shared slot");
        check(fixture.machine.peek(0xa03f) != 0 && fixture.machine.peek(0xcf00) == 99, "uses final slot at CF00");
        const auto before = fixture.machine.ram;
        check(fixture.put("FULL", {1}) == 2, "full allocation");
        check(std::equal(before.begin() + 0x2000, before.begin() + 0x5000, fixture.machine.ram.begin() + 0x2000), "full error preserves RAMFS");
        check(fixture.put("LAST", {88}) == 0, "overwrite when full");
        check(fixture.named(5, "D1/F7") == 0 && fixture.put("REUSE", {3}) == 0, "reuse slot");
        Fixture entries;
        for (unsigned index = 0; index < 16; ++index)
            check(entries.put("E" + std::to_string(index), {}) == 0, "16 empty entries without data allocation");
        check(entries.put("EXTRA", {}) == 2, "directory entry full");
    }
    void corruption()
    {
        for (const auto address : {0xa004, 0xa011, 0xa108, 0xa109, 0xa10a, 0xa012})
        {
            Fixture fixture;
            fixture.put("FILE", {1});
            fixture.machine.write(address, 255);
            const auto before = fixture.machine.ram;
            check(fixture.call(0) == 5, "reject corrupt metadata " + std::to_string(address));
            check(fixture.put("NEW", {2}) == 5, "reject mutation");
            check(std::equal(before.begin() + 0x2000, before.begin() + 0x5000, fixture.machine.ram.begin() + 0x2000), "no repair writes");
        }
        Fixture fixture;
        fixture.named(7, "DIR");
        fixture.machine.write(0xa052, 2);
        check(fixture.call(0) == 5, "reject directory parent cycle");
        Fixture shared;
        shared.put("ONE", {1});
        shared.put("TWO", {2});
        shared.machine.write(0xa11a, shared.machine.peek(0xa10a));
        check(shared.call(0) == 5, "reject shared storage ownership");
        Fixture names;
        names.put("ONE", {1});
        names.put("TWO", {2});
        for (unsigned index = 0; index < 8; ++index)
            names.machine.write(0xa110 + index, names.machine.peek(0xa100 + index));
        check(names.call(0) == 5, "reject duplicate names");
        Fixture old;
        old.machine.write(0xa004, 1);
        check(old.call(0) == 5 && old.machine.peek(0xa004) == 1, "old format requires explicit format");
    }
    void depth_and_reuse()
    {
        Fixture fixture;
        fixture.named(7, "DELETED");
        fixture.named(7, "PARENT");
        fixture.named(8, "DELETED");
        fixture.named(7, "PARENT/CHILD");
        check(fixture.named(9, "PARENT/CHILD") == 0 && fixture.pwd() == "/PARENT/CHILD", "pwd after directory slot reuse");
        fixture.named(9, "/");
        for (unsigned depth = 0; depth < 15; ++depth)
            check(fixture.named(7, "D") == 0 && fixture.named(9, "D") == 0, "nested depth creation");
        check(fixture.pwd() == "/D/D/D/D/D/D/D/D/D/D/D/D/D/D/D", "deep pwd");
        check(fixture.named(7, "EXTRA") == 4 && fixture.call(0) == 0, "depth limit preserves filesystem");
        check(fixture.named(9, "../../") == 0, "parent slash resolution");
    }
}
int main()
{
    try
    {
        directories();
        capacity();
        corruption();
        depth_and_reuse();
        std::cout << "12 KiB directory RAMFS tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}