#include "apple1/machine.hpp"
#include "apple1/ramfs_view.hpp"
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
                check((address >= 0x40 && address <= 0x5e) ||
                          (address >= 0x100 && address < 0x200) ||
                          (address >= 0x500 && address < 0x600) ||
                          (address >= 0xa00 && address <= 0xa48) ||
                          (address >= 0xd00 && address < 0xd30) ||
                          (address >= 0x1000 && address < 0x2000) ||
                          (address >= 0x6000 && address < 0x7000) ||
                          (address >= 0xb000 && address < 0xd000) ||
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
    void multi_block()
    {
        Fixture fixture;
        const auto write = [&](const std::string &name, const std::vector<uint8_t> &bytes) {
            fixture.name(name);
            fixture.machine.write(0x42, 0); fixture.machine.write(0x43, 0x10);
            fixture.machine.write(0x44, bytes.size() & 255);
            fixture.machine.write(0x57, bytes.size() >> 8);
            for (size_t index = 0; index < bytes.size(); ++index) fixture.machine.write(0x1000 + index, bytes[index]);
            return fixture.call(12);
        };
        const auto read = [&](const std::string &name) {
            fixture.name(name);
            fixture.machine.write(0x42, 0); fixture.machine.write(0x43, 0x60);
            check(fixture.call(13) == 0, "wide get");
            const unsigned size = fixture.machine.peek(0x44) | (unsigned(fixture.machine.peek(0x57)) << 8);
            std::vector<uint8_t> result(size);
            for (unsigned index = 0; index < size; ++index) result[index] = fixture.machine.peek(0x6000 + index);
            return result;
        };
        for (const unsigned size : {0, 1, 240, 241, 255, 256, 257, 511, 512, 513, 1024, 4096}) {
            std::vector<uint8_t> bytes(size);
            for (unsigned index = 0; index < size; ++index) bytes[index] = index * 37;
            check(write("LARGE", bytes) == 0, "wide put boundary " + std::to_string(size));
            check(read("LARGE") == bytes, "wide round trip " + std::to_string(size));
            check(fixture.call(0) == 0, "mount chain boundary");
        }
        fixture.name("LARGE");
        fixture.machine.write(0x42, 0); fixture.machine.write(0x43, 5);
        check(fixture.call(4) == 4, "bounded get rejects large file");
        check(fixture.named(5, "LARGE") == 0, "delete chain");
        for (unsigned slot = 2; slot < 48; ++slot) check(fixture.machine.peek(0xa010 + slot) == 0, "all blocks reclaimed");
        fixture.put("SMALL", {1});
        const auto first = fixture.machine.peek(0xa10a);
        fixture.machine.write(0xa090 + first, first);
        check(fixture.call(0) == 5, "overlong or cyclic chain rejected");
        fixture.call(1);
        std::vector<uint8_t> large(4096, 42);
        check(write("A", large) == 0 && write("B", large) == 0, "two 16-block files");
        std::vector<uint8_t> third(14 * 256, 77);
        check(write("C", third) == 0, "fill remaining blocks");
        check(write("NEW", {1}) == 2, "out of blocks");
        check(write("C", large) == 2 && read("C") == third, "failed growth preserves original chain");
        check(write("A", large) == 0 && read("A") == large, "overwrite reuses blocks on full volume");
        check(write("A", {9}) == 0 && read("A") == std::vector<uint8_t>{9}, "shrink releases tail");
        check(write("NEW", std::vector<uint8_t>(1024, 8)) == 0, "reuse freed tail blocks");
        fixture.call(1);
        check(write("CHAIN", std::vector<uint8_t>(513, 7)) == 0, "three-block chain");
        const auto first_block = fixture.machine.peek(0xa10a);
        const auto second_block = fixture.machine.peek(0xa090 + first_block);
        fixture.machine.write(0xa090 + second_block, first_block);
        check(fixture.call(0) == 5, "multi-block cycle rejected");
          check(!apple1::inspect_ramfs(fixture.machine).error.empty(), "viewer rejects multi-block cycle");
          fixture.call(1);
          std::vector<uint8_t> maximum(46 * 256);
          for (unsigned index = 0; index < maximum.size(); ++index)
            maximum[index] = static_cast<uint8_t>(index * 17);
          std::copy(maximum.begin(), maximum.end(), fixture.machine.rom.begin() + 0x3000);
          fixture.name("MAXIMUM");
          fixture.machine.write(0x42, 0); fixture.machine.write(0x43, 0x30);
          fixture.machine.write(0x44, 0); fixture.machine.write(0x57, 46);
          check(fixture.call(12) == 0 && fixture.call(0) == 0, "single file fills all 46 blocks");
          const auto maximum_view = apple1::inspect_ramfs(fixture.machine);
          check(maximum_view.error.empty() && maximum_view.files.size() == 1 &&
              maximum_view.files.front().data == maximum && maximum_view.files.front().blocks.size() == 46,
              "viewer reads maximum file and complete block list");
          const auto chain_first = fixture.machine.peek(0xa10a);
          fixture.machine.write(0xa108, 1);
          fixture.machine.write(0xa10b, 0);
          check(fixture.call(0) == 5 && !apple1::inspect_ramfs(fixture.machine).error.empty(),
              "guest and viewer reject chain longer than file size");
          check(chain_first >= 2, "maximum chain head");
    }
    void directories()
    {
        Fixture fixture;
        check(fixture.machine.peek(0xa004) == 3 && fixture.machine.peek(0xa011) == 2, "v3 root");
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
        for (const auto address : {0xa004, 0xa011, 0xa10b, 0xa109, 0xa10a, 0xa012})
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
        multi_block();
        std::cout << "12 KiB directory RAMFS tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}