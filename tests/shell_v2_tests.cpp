#include "apple1/machine.hpp"
#include "apple1/ramfs_view.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void check(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    struct Shell
    {
        apple1::Machine machine;
        std::string output;
        bool force_overflow = false;
        Shell()
        {
            machine.load_rom(APPLE1_SHELL_ROM);
            machine.output = [this](char value)
            { output += value; };
            machine.cpu.write = [this](uint16_t address, uint8_t value)
            {
                check((address >= 0x20 && address <= 0x3d) ||
                          (address >= 0x40 && address <= 0x56) ||
                          (address >= 0x100 && address < 0x200) ||
                          (address >= 0x300 && address <= 0x3ef) ||
                          (address >= 0x400 && address <= 0x45f) ||
                          (address >= 0x500 && address < 0x5f0) ||
                          (address >= 0x700 && address <= 0x77f) ||
                          (address >= 0x800 && address < 0x8f0) ||
                          (address >= 0x900 && address < 0x9f0) ||
                          (address >= 0xa00 && address <= 0xa48) ||
                          (address >= 0xa000 && address < 0xd000) ||
                          (address >= 0xd010 && address <= 0xd013),
                      "shell write boundary");
                if (force_overflow && address == 0x2d && value == 1)
                {
                    value = 240;
                    force_overflow = false;
                }
                machine.write(address, value);
            };
            machine.reset();
            wait(0);
        }
        void wait(size_t start)
        {
            const auto began = machine.ticks();
            const auto ready = [&]()
            {
                const auto line = output.rfind('\r');
                return line != std::string::npos && line >= start && line + 1 < output.size() &&
                       (output[line + 1] == '/' || output[line + 1] == '?') && output.ends_with("> ");
            };
            while (!ready() && machine.ticks() - began < 10000000)
                machine.step();
            check(ready(), "shell timeout: " + output.substr(start));
        }
        std::string command(const std::string &line)
        {
            const auto start = output.size();
            for (char value : line + '\r')
            {
                machine.enqueue(std::string(1, value));
                const auto began = machine.ticks();
                while (machine.ticks() - began < 100000)
                    machine.step();
            }
            wait(start);
            return output.substr(start);
        }
        std::string file(const std::string &path)
        {
            const auto before = machine.ram;
            const auto view = apple1::inspect_ramfs(machine);
            check(before == machine.ram, "viewer side effects");
            check(view.error.empty(), "viewer: " + view.error);
            for (const auto &entry : view.files)
                if (entry.path == path && !entry.directory)
                    return {entry.data.begin(), entry.data.end()};
            throw std::runtime_error("file missing: " + path);
        }
    };
    void workflow()
    {
        Shell shell;
        check(shell.output.find("A1OS\rRAMFS FORMATTED") != std::string::npos, "boot");
        check(shell.output.ends_with("\r/> "), "root path prompt");
        check(shell.command("HELP").find("MKDIR RMDIR CD PWD") != std::string::npos, "help directories");
        check(shell.command("PWD").find("\r/\r") != std::string::npos, "pwd root");
        check(shell.command("MKDIR DOCS").find("ERROR") == std::string::npos, "mkdir docs");
        check(shell.command("MKDIR DOCS").find("ERROR 10") != std::string::npos, "mkdir exists");
        shell.command("MKDIR DOCS/SUB");
        shell.command("ECHO ROOT > NOTE");
        shell.command("ECHO HELLO > /DOCS/NOTE");
        check(shell.file("/NOTE") == "ROOT\r" && shell.file("/DOCS/NOTE") == "HELLO\r", "same name different directories");
        check(shell.command("LS DOCS").find("SUB/") != std::string::npos, "ls path");
        check(shell.command("PWD").find("\r/\r") != std::string::npos, "ls does not change cwd");
        shell.command("CD DOCS/SUB");
        check(shell.output.ends_with("\r/DOCS/SUB> "), "nested path prompt");
        check(shell.command("PWD").find("\r/DOCS/SUB\r") != std::string::npos, "nested pwd");
        check(shell.command("CAT ../NOTE").find("\rHELLO\r") != std::string::npos, "parent relative read");
        shell.command("CAT ../NOTE | WRITE COPY");
        check(shell.file("/DOCS/SUB/COPY") == "HELLO\r", "pipe relative destination");
        shell.command("CAT COPY | CAT > /DOCS/OTHER");
        check(shell.file("/DOCS/OTHER") == "HELLO\r", "pipe absolute destination");
        check(shell.command("RMDIR /DOCS/SUB").find("ERROR 11") != std::string::npos, "rmdir current directory");
        shell.command("CD");
        check(shell.command("PWD").find("\r/\r") != std::string::npos, "cd no argument root");
        check(shell.command("RMDIR DOCS").find("ERROR 11") != std::string::npos, "nonempty directory");
        check(shell.command("RM DOCS").find("ERROR 9") != std::string::npos, "rm directory error");
        check(shell.command("CAT DOCS").find("ERROR 9") != std::string::npos, "cat directory error");
        check(shell.command("CD NOTE").find("ERROR 9") != std::string::npos, "cd file error");
        check(shell.command("ECHO BAD > DOCS").find("ERROR 9") != std::string::npos, "redirect directory error");
        check(shell.command("ECHO BAD > MISSING/FILE").find("ERROR 1") != std::string::npos, "missing parent");
        shell.command("RM DOCS/SUB/COPY");
        check(shell.command("RMDIR DOCS/SUB").find("ERROR") == std::string::npos, "remove empty directory");
        shell.command("CD DOCS");
        const auto start = shell.output.size();
        shell.machine.reset();
        shell.wait(start);
        check(shell.output.substr(start).find("FORMATTED") == std::string::npos, "reset preserves filesystem");
        check(shell.command("PWD").find("\r/\r") != std::string::npos, "reset cwd root");
        check(shell.file("/DOCS/NOTE") == "HELLO\r", "reset nested retention");
        check(shell.command("MKDIR X | CAT").find("ERROR 3") != std::string::npos, "directory mutation pipeline forbidden");
        check(shell.command("ECHO X > TOOLONG99/N").find("SYNTAX ERROR") != std::string::npos, "component length preflight");
        check(shell.command("ECHO X >> NOTE").find("SYNTAX ERROR") != std::string::npos, "unsupported syntax");
        shell.command("CAT NOTE > NOTE");
        check(shell.file("/NOTE") == "ROOT\r", "self redirect");
        check(shell.command("CAT MISSING | WRITE NOTE").find("ERROR 1") != std::string::npos, "pipe failure");
        check(shell.file("/NOTE") == "ROOT\r", "pipe failure preserves destination");
        shell.force_overflow = true;
        check(shell.command("ECHO X > NOTE").find("ERROR 8") != std::string::npos, "overflow guard");
        check(shell.file("/NOTE") == "ROOT\r", "overflow preserves destination");
        shell.command("FORMAT");
        check(shell.file("/NOTE") == "ROOT\r", "format confirmation");
        shell.command("FORMAT YES");
        check(apple1::inspect_ramfs(shell.machine).files.empty(), "format clears directories");
        for (unsigned index = 0; index < 16; ++index)
            shell.command("WRITE E" + std::to_string(index));
        check(shell.command("WRITE FULL").find("ERROR 2") != std::string::npos, "directory full");
        shell.command("FORMAT YES");
        check(shell.command("ECHO AX\bB").find("\rAB\r") != std::string::npos, "editing");
        check(shell.command(std::string(240, 'X')).find("LINE TOO LONG") != std::string::npos, "overflow line");
        shell.command("MKDIR CANCEL\x03");
        check(apple1::inspect_ramfs(shell.machine).files.empty(), "cancel");
        shell.machine.write(0xa004, 1);
        const auto old = shell.output.size();
        shell.machine.reset();
        shell.wait(old);
        check(shell.output.substr(old).find("RAMFS INVALID") != std::string::npos && shell.machine.peek(0xa004) == 1,
              "v1 requires explicit reformat");
        check(!apple1::inspect_ramfs(shell.machine).error.empty(), "viewer rejects old format");
        shell.command("FORMAT YES");
        shell.machine.write(0xa004, 0);
        shell.machine.write(0xcfff, 42);
        const auto corrupt = shell.output.size();
        shell.machine.reset();
        shell.wait(corrupt);
        check(shell.machine.peek(0xcfff) == 42 && shell.output.substr(corrupt).find("RAMFS INVALID") != std::string::npos,
              "boot preserves nonempty data at end of 12 KiB");
        const auto cold = shell.output.size();
        shell.machine.power_cycle();
        shell.wait(cold);
        check(shell.output.substr(cold).find("FORMATTED") != std::string::npos, "cold boot");
    }
}
int main()
{
    try
    {
        workflow();
        std::cout << "Directory shell and stream integration passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}