#include "apple1/machine.hpp"
#include "apple1/ramfs_view.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
struct Shell {
    apple1::Machine machine;
    std::string output;
    Shell() {
        machine.load_rom(APPLE1_SHELL_ROM);
        machine.output = [this](char value) { output += value; };
        machine.cpu.write = [this](uint16_t address, uint8_t value) {
            check((address >= 0x20 && address <= 0x2b) ||
                  (address >= 0x40 && address <= 0x4b) ||
                  (address >= 0x100 && address < 0x200) ||
                  (address >= 0x300 && address <= 0x3ef) ||
                  (address >= 0x400 && address <= 0x408) ||
                  (address >= 0x500 && address < 0x5f0) ||
                  (address >= 0xa000 && address < 0xb000) ||
                  (address >= 0xd010 && address <= 0xd013), "shell wrote outside reserved regions");
            machine.write(address, value);
        };
        machine.reset();
        wait_prompt(0);
    }
    void wait_prompt(size_t start) {
        const auto began = machine.ticks();
        while (output.find("> ", start) == std::string::npos && machine.ticks() - began < 10000000)
            machine.step();
        check(output.find("> ", start) != std::string::npos, "shell timeout: " + output.substr(start));
    }
    std::string command(const std::string& text) {
        const auto start = output.size();
        for (char value : text + '\r') {
            machine.enqueue(std::string(1, value));
            const auto began = machine.ticks();
            while (machine.ticks() - began < 100000) machine.step();
        }
        wait_prompt(start);
        return output.substr(start);
    }
};
}
int main() {
    try {
        Shell shell;
        check(shell.output.find("A1OS\rRAMFS FORMATTED") != std::string::npos, "cold boot");
        check(shell.command("HELP").find("WRITE CAT RM FORMAT") != std::string::npos, "help");
        check(shell.command("  ECHO HELLO").find("\rHELLO\r") != std::string::npos, "echo and leading spaces");
        check(shell.command("").find("UNKNOWN") == std::string::npos, "empty input");
        check(shell.command("BOGUS").find("UNKNOWN COMMAND") != std::string::npos, "unknown command");
        check(shell.command("WRITE NOTE HELLO WORLD").find("ERROR") == std::string::npos, "write");
        check(shell.command("LS").find("NOTE 011") != std::string::npos, "list name and size");
        check(shell.command("CAT NOTE").find("\rHELLO WORLD\r") != std::string::npos, "cat");
          const auto before_view = shell.machine.ram;
          const auto view = apple1::inspect_ramfs(shell.machine);
          check(view.error.empty() && view.files.size() == 1 && view.files[0].name == "NOTE" &&
              std::string(view.files[0].data.begin(), view.files[0].data.end()) == "HELLO WORLD",
              "file viewer reads guest-created file");
          check(before_view == shell.machine.ram, "viewer must not mutate guest RAM");
        shell.machine.write(0xa108, 255);
        check(!apple1::inspect_ramfs(shell.machine).error.empty(), "viewer rejects invalid length");
        shell.machine.write(0xa108, 11);
        shell.machine.write(0xa105, 'X');
        check(!apple1::inspect_ramfs(shell.machine).error.empty(), "viewer rejects bad padding");
        shell.machine.write(0xa105, 0);
        for (unsigned index = 0; index < 8; ++index)
            shell.machine.write(0xa200 + index, shell.machine.peek(0xa100 + index));
        shell.machine.write(0xa208, 0);
        check(!apple1::inspect_ramfs(shell.machine).error.empty(), "viewer rejects duplicate names");
        shell.machine.write(0xa200, 0);
        shell.command("WRITE NOTE SHORT");
        check(shell.command("CAT NOTE").find("\rSHORT\r") != std::string::npos, "overwrite");
        shell.command("WRITE EMPTY");
        check(shell.command("LS").find("EMPTY 000") != std::string::npos, "empty file");
        check(shell.command("WRITE TOOLONG99 TEXT").find("ERROR 3") != std::string::npos, "long name");
        check(shell.command("CAT NOTE EXTRA").find("ERROR 3") != std::string::npos, "extra arguments");
        const auto start = shell.output.size();
        shell.machine.reset();
        shell.wait_prompt(start);
        check(shell.output.substr(start).find("FORMATTED") == std::string::npos, "reset must not format");
        check(shell.command("CAT NOTE").find("\rSHORT\r") != std::string::npos, "reset retains file");
        shell.command("RM NOTE");
        check(shell.command("CAT NOTE").find("ERROR 1") != std::string::npos, "remove");
        shell.command("FORMAT");
        check(shell.command("LS").find("EMPTY 000") != std::string::npos, "format requires confirmation");
        shell.command("FORMAT YES");
        check(shell.command("CAT EMPTY").find("ERROR 1") != std::string::npos, "confirmed format");
        for (unsigned index = 0; index < 15; ++index)
            check(shell.command("WRITE F" + std::to_string(index) + " X").find("ERROR") == std::string::npos,
                  "fill filesystem from shell");
        check(shell.command("WRITE EXTRA X").find("ERROR 2") != std::string::npos, "shell reports full filesystem");
        shell.command("FORMAT YES");
        shell.command("WRITE KEEP SAFE");
        check(shell.command(std::string(240, 'X')).find("LINE TOO LONG") != std::string::npos, "overflow line rejected");
        check(shell.command("ECHO AX\bB").find("\rAB\r") != std::string::npos, "backspace editing");
        check(shell.command("RM KEEP\x03").find("ERROR") == std::string::npos, "cancel line");
        check(shell.command("CAT KEEP").find("\rSAFE\r") != std::string::npos, "cancel prevented command");
        shell.machine.write(0xa004, 99);
        check(!apple1::inspect_ramfs(shell.machine).error.empty(), "viewer rejects invalid version");
        const auto corrupt_start = shell.output.size();
        shell.machine.reset();
        shell.wait_prompt(corrupt_start);
        check(shell.output.substr(corrupt_start).find("RAMFS INVALID") != std::string::npos, "corrupt mount warning");
        check(shell.machine.peek(0xa004) == 99, "corruption must not autoformat");
        check(shell.command("LS").find("ERROR 5") != std::string::npos, "corrupt filesystem error");
        shell.command("FORMAT YES");
        for (uint16_t address = 0xa000; address < 0xb000; ++address)
            shell.machine.write(address, 0);
        shell.machine.write(0xafff, 42);
        const auto orphan_start = shell.output.size();
        shell.machine.reset();
        shell.wait_prompt(orphan_start);
        check(shell.output.substr(orphan_start).find("RAMFS INVALID") != std::string::npos &&
              shell.machine.peek(0xafff) == 42, "nonempty RAM without header must not autoformat");
        const auto cold_start = shell.output.size();
        shell.machine.power_cycle();
        shell.wait_prompt(cold_start);
        check(shell.output.substr(cold_start).find("RAMFS FORMATTED") != std::string::npos, "power-cycle boot");
        std::cout << "Bootable shell command and RAMFS integration tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}