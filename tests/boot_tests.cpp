#include "apple1/machine.hpp"
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef APPLE1_SOURCE_DIR
#define APPLE1_SOURCE_DIR "."
#endif

namespace
{
    void check(bool result, const std::string &message)
    {
        if (!result)
            throw std::runtime_error(message);
    }

    class Boot
    {
    public:
        apple1::Machine machine;
        std::string output;

        explicit Boot(apple1::Profile profile) : machine(profile)
        {
            machine.load_rom(std::filesystem::path(APPLE1_SOURCE_DIR) / "roms" /
                             (profile == apple1::Profile::Expansion ? "32KA1COMPIL.BIN" : "wozmon.rom"));
            machine.output = [this](char value)
            { output += value; };
            machine.reset();
        }

        void expect(std::string_view text, size_t since = 0)
        {
            const auto start = machine.ticks();
            while (output.find(text, since) == std::string::npos && machine.ticks() - start < 30000000)
                machine.step();
            if (output.find(text, since) == std::string::npos)
            {
                std::ostringstream message;
                message << "Expected " << std::quoted(std::string(text)) << " within 30M cycles; PC=$"
                        << std::hex << machine.cpu.pc << "\nOutput since command:\n"
                        << output.substr(since);
                throw std::runtime_error(message.str());
            }
        }

        size_t send(std::string text)
        {
            const auto start = output.size();
            for (char value : text)
            {
                machine.enqueue(std::string(1, value));
                const auto tick = machine.ticks();
                while (machine.ticks() - tick < 100000)
                    machine.step();
            }
            return start;
        }

        void tiny_program(char letter)
        {
            const std::string byte = letter == 'A' ? "C1" : "C2";
            auto start = send("0300: A9 " + byte + " 20 EF FF 4C 00 FF\r");
            expect("0300:", start);
            start = send("0300R\r");
            expect(std::string(1, letter) + "\\\r", start);
            check(machine.peek(0x0300) == 0xa9 && machine.peek(0x0301) == (letter | 0x80) &&
                      machine.peek(0x0305) == 0x4c && machine.peek(0x0307) == 0xff,
                  "WozMon deposit did not preserve the program bytes");
        }
    };

    void expansion()
    {
        Boot boot(apple1::Profile::Expansion);
        check(boot.machine.cpu.pc == 0x2000, "Expansion reset vector must be $2000");
        boot.expect("YOUR CHOICE");
        auto start = boot.send("W");
        boot.expect("\\", start);
        boot.tiny_program('A');
        boot.machine.reset();
        start = boot.output.size();
        boot.expect("YOUR CHOICE", start);
        start = boot.send("I");
        boot.expect(">", start);
        start = boot.send("PRINT 2+3\r");
        boot.expect("\r5\r", start);
        std::cout << "PASS expansion: unmodified reset, menu, WozMon, program A, BASIC PRINT 2+3\n";
    }

    void standard()
    {
        Boot boot(apple1::Profile::Standard);
        check(boot.machine.cpu.pc == 0xff00, "Standard reset vector must be $FF00");
        boot.expect("\\");
        boot.tiny_program('B');
        std::cout << "PASS standard: original WozMon boot, deposit and execute program B\n";
    }

    void menu_programs()
    {
        Boot boot(apple1::Profile::Expansion);
        for (const std::string choice : {"A", "B"})
        {
            boot.machine.reset();
            boot.expect("YOUR CHOICE", boot.output.size());
            const auto start = boot.send(choice);
            boot.expect("PASS 01", start);
            std::cout << "PASS expansion menu: " << (choice == "A" ? "8K" : "4K") << " memory test\n";
        }
        boot.machine.reset();
        boot.expect("YOUR CHOICE", boot.output.size());
        auto start = boot.send("C");
        boot.expect("ABCDEFGHIJKLMNOPQRSTUVWXYZ", start);
        std::cout << "PASS expansion menu: display test\n";
        boot.machine.power_cycle();
        boot.expect("YOUR CHOICE", boot.output.size());
        start = boot.send("5");
        boot.expect("PRESS KEY TO START", start);
        boot.send(" ");
        const auto started = boot.machine.ticks();
        while (boot.machine.ticks() - started < 2000000)
            boot.machine.step();
        size_t populated = 0;
        for (const auto value : boot.machine.ram)
            if (value != 0)
                ++populated;
        check(populated != 0, "C64 Maze must populate RAM after a cold start");
        const auto tick = boot.machine.ticks();
        while (boot.machine.owner(boot.machine.cpu.pc) != "Card RAM" &&
               boot.machine.ticks() - tick < 30000000)
            boot.machine.step();
        check(boot.machine.owner(boot.machine.cpu.pc) == "Card RAM", "Copied program must execute in RAM");
        std::cout << "PASS expansion menu: copied C64 Maze program starts and executes in RAM\n";
    }
}

int main()
{
    unsigned failures = 0;
    for (const auto test : {expansion, standard, menu_programs})
    {
        try
        {
            test();
        }
        catch (const std::exception &error)
        {
            ++failures;
            std::cerr << "FAIL: " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}