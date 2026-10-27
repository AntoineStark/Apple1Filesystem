#include "apple1/machine.hpp"
#include "apple1/gui_input.hpp"
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
                          (address >= 0x40 && address <= 0x6f) ||
                          (address >= 0x100 && address < 0x200) ||
                          (address >= 0x300 && address <= 0x3ef) ||
                          (address >= 0x400 && address <= 0x45f) ||
                          (address >= 0x500 && address < 0x5f0) ||
                          (address >= 0x700 && address <= 0x77f) ||
                          (address >= 0x800 && address < 0x8f0) ||
                          (address >= 0x900 && address < 0x9f0) ||
                          (address >= 0xa00 && address <= 0xa48) ||
                          (address >= 0xb00 && address <= 0xbe3) ||
                          (address >= 0xc00 && address <= 0xc28) ||
                          (address >= 0xd00 && address < 0xd30) ||
                          (address >= 0xe00 && address < 0xef0) ||
                          (address >= 0x1000 && address < 0x2000) ||
                          (address >= 0x6000 && address < 0x6800) ||
                          (address >= 0xa000 && address < 0xd000) ||
                          (address >= 0xd010 && address <= 0xd013),
                      "shell write boundary");
                if (force_overflow && address == 0x2d && value == 1)
                {
                    machine.write(0x64, 4);
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
            while (!ready() && machine.ticks() - began < 120000000)
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
    void transfers()
    {
        Shell shell;
        const auto success = [&](const std::string &line)
        {
            const auto result = shell.command(line);
            check(result.find("ERROR") == std::string::npos, line + ": " + result);
        };
        const auto absent = [&](const std::string &path)
        {
            const auto view = apple1::inspect_ramfs(shell.machine);
            check(view.error.empty(), "valid filesystem after move");
            for (const auto &entry : view.files)
                check(entry.path != path, "source still present: " + path);
        };
        success("ECHO ORIGINAL > SOURCE");
        success("MKDIR DOCS");
        success("CP SOURCE DOCS");
        check(shell.file("/DOCS/SOURCE") == "ORIGINAL\r", "directory destination retains basename");
        success("CP SOURCE NEW");
        success("ECHO OLD > TARGET");
        success("CP SOURCE TARGET");
        check(shell.file("/TARGET") == "ORIGINAL\r", "copy overwrites existing file");
        success("CP SOURCE ./SOURCE");
        success("MV SOURCE /SOURCE");
        check(shell.file("/SOURCE") == "ORIGINAL\r", "self aliases preserve source");
        success("MV NEW RENAMED");
        absent("/NEW");
        check(shell.file("/RENAMED") == "ORIGINAL\r", "rename file");
        success("ECHO REPLACED > DOCS/RENAMED");
        success("MV RENAMED DOCS");
        absent("/RENAMED");
        check(shell.file("/DOCS/RENAMED") == "ORIGINAL\r", "move into directory overwrites basename");
        success("MV TARGET DOCS/OTHER");
        absent("/TARGET");
        check(shell.file("/DOCS/OTHER") == "ORIGINAL\r", "move to explicit file path");
        success("CD DOCS");
        success("CP ../SOURCE LOCAL");
        success("MV LOCAL ..");
        absent("/DOCS/LOCAL");
        check(shell.file("/LOCAL") == "ORIGINAL\r", "parent directory target");
        success("CP SOURCE .");
        success("MV SOURCE ./");
        check(shell.file("/DOCS/SOURCE") == "ORIGINAL\r", "self via directory target");
        success("CD /");
        for (const auto &line : {"CP SOURCE", "MV SOURCE TARGET EXTRA", "CP SOURCE DOCS | CAT", "MV SOURCE > TARGET"})
            check(shell.command(line).find("ERROR") != std::string::npos, "reject transfer syntax");
        check(shell.command("CP MISSING NEW").find("ERROR 1") != std::string::npos, "missing source");
        check(shell.command("MV SOURCE MISSING/FILE").find("ERROR 1") != std::string::npos, "missing target parent");
        check(shell.file("/SOURCE") == "ORIGINAL\r", "failed move preserves source");
        check(shell.command("CP DOCS TREE").find("ERROR 9") != std::string::npos, "directory copy explicitly unsupported");
        check(shell.command("MV DOCS TREE").find("ERROR 9") != std::string::npos, "directory move explicitly unsupported");
        success("WRITE EMPTY");
        success("MKDIR DOCS/VOID");
        success("WRITE VOID SAFE");
        check(shell.command("MV VOID DOCS").find("ERROR 9") != std::string::npos, "basename destination directory rejected");
        check(shell.file("/VOID") == "SAFE", "type conflict preserves source");
        success("CP EMPTY DOCS");
        check(shell.file("/DOCS/EMPTY").empty(), "copy empty file");
        success("MV EMPTY VOID");
        absent("/EMPTY");
        check(shell.file("/VOID").empty(), "move empty file");
        success("FORMAT YES");
        success("WRITE SOURCE X");
        const auto directory = apple1::inspect_ramfs(shell.machine);
        const auto &source = directory.files.front();
        const auto metadata = 0xa000 + source.parent_slot * 256 + (source.slot % 16) * 16;
        shell.machine.write(metadata + 8, 240);
        std::string binary;
        for (unsigned index = 0; index < 240; ++index)
        {
            shell.machine.write(0xa000 + source.data_slot * 256 + index, index);
            binary += static_cast<char>(index);
        }
        success("CP SOURCE BINARY");
        success("MV BINARY MOVED");
        check(shell.file("/MOVED") == binary && shell.file("/SOURCE") == binary, "240-byte binary transfer");
        success("FORMAT YES");
        for (unsigned index = 0; index < 16; ++index)
            success("WRITE F" + std::to_string(index) + " X");
        check(shell.command("MV F0 EXTRA").find("ERROR 2") != std::string::npos, "full directory rejects new name");
        check(shell.file("/F0") == "X", "directory-full move preserves source");
        success("MV F0 F1");
        absent("/F0");
        success("FORMAT YES");
        for (unsigned dir = 0; dir < 3; ++dir)
        {
            success("MKDIR D" + std::to_string(dir));
            for (unsigned file = 0; file < 14; ++file)
                success("WRITE D" + std::to_string(dir) + "/F" + std::to_string(file) + " DATA");
        }
        success("WRITE LAST LAST");
        check(shell.command("CP LAST NEW").find("ERROR 2") != std::string::npos, "full storage copy fails");
        check(shell.command("MV LAST D0/NEW").find("ERROR") == std::string::npos, "move needs no free data slot");
        absent("/LAST");
        success("MV D0/NEW RENAMED");
        check(shell.file("/RENAMED") == "LAST", "rename on full storage");
        success("CP RENAMED D1/F0");
        check(shell.file("/D1/F0") == "LAST", "overwrite copy on full storage");
        const auto before = shell.output.size();
        shell.machine.reset();
        shell.wait(before);
        check(shell.file("/RENAMED") == "LAST", "transfer reset retention");
    }
    void append_redirection()
    {
        Shell shell;
        shell.command("ECHO FIRST >> NOTE");
        check(shell.file("/NOTE") == "FIRST\r", "append creates missing file");
        shell.command("ECHO SECOND>>NOTE");
        check(shell.file("/NOTE") == "FIRST\rSECOND\r", "append retains old contents");
        shell.command("ECHO THIRD | GREP THIRD >> NOTE");
        check(shell.file("/NOTE") == "FIRST\rSECOND\rTHIRD\r", "pipeline append");
        shell.command("MKDIR DOCS");
        shell.command("CD DOCS");
        shell.command("CAT ../NOTE >> COPY");
        check(shell.file("/DOCS/COPY") == shell.file("/NOTE"), "relative append target");
        shell.command("CAT COPY >> /DOCS/COPY");
        check(shell.file("/DOCS/COPY") == shell.file("/NOTE") + shell.file("/NOTE"), "self append snapshots source");
        shell.command("WRITE EMPTY");
        shell.command("CAT EMPTY >> COPY");
        check(shell.file("/DOCS/COPY").size() == 38, "empty output preserves existing file");
        shell.command("CAT EMPTY >> ZERO");
        check(shell.file("/DOCS/ZERO").empty(), "empty append creates empty file");
        const auto saved = shell.file("/DOCS/COPY");
        check(shell.command("CAT MISSING >> COPY").find("ERROR 1") != std::string::npos, "failed producer append");
        check(shell.file("/DOCS/COPY") == saved, "failed producer preserves file");
        check(shell.command("ECHO X >> MISSING/FILE").find("ERROR 1") != std::string::npos, "missing parent append");
        check(shell.command("ECHO X >> /DOCS").find("ERROR 9") != std::string::npos, "append directory rejected");
        for (const auto &line : {"ECHO X >>", "ECHO X >>> COPY", "ECHO X >> COPY > ZERO",
                                 "ECHO X >> COPY | CAT", "RM COPY >> ZERO", "WRITE COPY X >> ZERO"})
            check(shell.command(line).find("ERROR") != std::string::npos, "invalid append: " + std::string(line));
        check(shell.file("/DOCS/COPY") == saved, "invalid syntax preserves contents");
        shell.command("WRITE LIMIT X");
        const auto set_limit = [&](unsigned size)
        {
            const auto view = apple1::inspect_ramfs(shell.machine);
            for (const auto &file : view.files)
                if (file.path == "/DOCS/LIMIT")
                {
                    shell.machine.write(0xa000 + file.parent_slot * 256 + file.slot % 16 * 16 + 8, size);
                    for (unsigned index = 0; index < size; ++index)
                        shell.machine.write(0xa000 + file.data_slot * 256 + index, index);
                    return;
                }
            throw std::runtime_error("missing append limit fixture");
        };
        set_limit(238);
        const auto binary = shell.file("/DOCS/LIMIT");
        shell.command("ECHO Z >> LIMIT");
        check(shell.file("/DOCS/LIMIT") == binary + "Z\r", "exact 240-byte append preserves binary data");
        check(shell.command("ECHO Z >> LIMIT").find("ERROR") == std::string::npos, "append beyond old limit");
        check(shell.file("/DOCS/LIMIT") == binary + "Z\rZ\r", "append preserves old bytes");
        shell.command("CAT EMPTY >> LIMIT");
        check(shell.file("/DOCS/LIMIT").size() == 242, "empty append to large file");
        check(shell.command("CAT LIMIT >> LIMIT").find("ERROR") == std::string::npos, "wide self-append input");
        check(shell.file("/DOCS/LIMIT") == binary + "Z\rZ\r" + binary + "Z\rZ\r", "self-append doubles large contents");
        shell.command("FORMAT YES");
        for (unsigned index = 0; index < 16; ++index)
            shell.command("WRITE E" + std::to_string(index));
        check(shell.command("ECHO X >> NEW").find("ERROR 2") != std::string::npos, "append directory full");
        check(shell.file("/E0").empty(), "full directory leaves other entries intact");
    }
    void large_files()
    {
        Shell shell;
        const std::string chunk = std::string(199, 'A') + '\r';
        for (unsigned index = 0; index < 20; ++index)
            check(shell.command("ECHO " + std::string(199, 'A') + " >> LARGE").find("ERROR") == std::string::npos,
                  "append 200-byte chunk across blocks");
        std::string expected;
        for (unsigned index = 0; index < 20; ++index)
            expected += chunk;
        check(shell.file("/LARGE") == expected, "4000-byte append contents");
        check(shell.command("ECHO " + std::string(95, 'B') + " >> LARGE").find("ERROR") == std::string::npos,
              "append to exact 4096-byte buffer limit");
        expected += std::string(95, 'B') + '\r';
        check(shell.file("/LARGE") == expected, "4096-byte exact data");
        check(shell.command("LS").find("LARGE 04096") != std::string::npos, "wide listing size");
        check(shell.command("CAT LARGE").find(expected) != std::string::npos, "direct cat multi-block file");
        check(shell.command("CP LARGE COPY").find("ERROR") == std::string::npos && shell.file("/COPY") == expected,
              "copy multi-block file");
        check(shell.command("MV COPY MOVED").find("ERROR") == std::string::npos && shell.file("/MOVED") == expected,
              "move multi-block chain");
        check(shell.command("ECHO X >> LARGE").find("ERROR 8") != std::string::npos && shell.file("/LARGE") == expected,
              "large append overflow preserves old file");
          check(shell.command("CAT LARGE | WRITE SMALL").find("ERROR 8") != std::string::npos,
              "large file never silently truncates into pipe");
          check(shell.command("WC LARGE").find("\r021 021 04096\r") != std::string::npos, "wide wc counts");
        shell.command("WRITE TARGET OLD");
        check(shell.command("MV MOVED TARGET").find("ERROR") == std::string::npos && shell.file("/TARGET") == expected,
              "move replacement transfers whole chain");
        shell.command("RM TARGET");
        check(shell.command("CP LARGE REUSED").find("ERROR") == std::string::npos && shell.file("/REUSED") == expected,
              "delete reclaims complete chain");
    }
    void demo_and_editor()
    {
        check(apple1::editor_line("ONE\nTWO\nTHREE", 1).text == "ONE", "caret first line");
        const auto middle = apple1::editor_line("ONE\nTWO\nTHREE", 5);
        check(middle.text == "TWO" && middle.next_cursor == 8, "caret middle line only");
        check(apple1::editor_line("ONE\r\nTWO", 1).text == "ONE", "CRLF editor line");
        check(apple1::editor_line("ONE\n\nTWO", 4).text.empty(), "empty current line");
        check(apple1::editor_line("ONE", 99).text == "ONE", "clamp cursor");
        check(apple1::editor_line("ONE\n", 4).text.empty(), "trailing empty line");
        Shell shell;
        size_t cursor = 0;
        while (cursor < apple1::shell_demo.size())
        {
            const auto line = apple1::editor_line(apple1::shell_demo, cursor);
            const auto result = shell.command(line.text);
            check(result.find("ERROR") == std::string::npos && result.find("UNKNOWN COMMAND") == std::string::npos,
                  "demo command failed: " + line.text + ": " + result);
            cursor = line.next_cursor;
        }
        check(shell.file("/DEMO/NOTE") == "HELLO WORLD\rSECOND LINE\r", "demo note and append");
        check(shell.file("/DEMO/MATCH") == "HELLO WORLD\r", "demo grep");
        check(shell.file("/DEMO/SUB/SAVED") == "HELLO WORLD\rSECOND LINE\r", "demo move");
        check(shell.file("/DEMO/SUB/COPY") == "SECOND LINE\r", "demo parent-path filter pipeline");
        const auto demo_view = apple1::inspect_ramfs(shell.machine);
        check(demo_view.files.size() == 9, "demo final tree");
        check(shell.file("/DEMO/LARGE").size() > 256, "demo multi-block file");
        for (const auto &file : demo_view.files)
            if (file.path == "/DEMO/LARGE")
                check(file.blocks.size() >= 2, "viewer chain blocks");
        check(shell.output.ends_with("\r/> "), "demo returns to root");
    }
    void filters()
    {
        Shell shell;
          check(shell.command("HELP | GREP RM").find("\rRM FILE - REMOVE FILE\r") != std::string::npos,
              "full help pipeline");
          shell.command("HELP > COMMANDS");
          check(shell.file("/COMMANDS").size() > 256, "help capture spans pages");
          check(shell.command("GREP RM COMMANDS").find("\rRM FILE - REMOVE FILE\r") != std::string::npos,
              "grep standalone file");
          shell.command("GREP RM COMMANDS > FOUND");
          check(shell.file("/FOUND").find("RM FILE - REMOVE FILE\r") != std::string::npos, "file grep redirected");
          shell.command("GREP RM COMMANDS | WC > COUNTS");
          check(!shell.file("/COUNTS").empty(), "file grep followed by wc");
          check(shell.command("GREP RM MISSING").find("ERROR 1") != std::string::npos, "grep missing file");
          check(shell.command("GREP RM COMMANDS EXTRA").find("ERROR 3") != std::string::npos, "grep extra filename");
        shell.command("ECHO HELLO WORLD > NOTE");
        check(shell.command("WC NOTE").find("\r001 002 012\r") != std::string::npos, "wc file");
        check(shell.command("CAT NOTE | WC").find("\r001 002 012\r") != std::string::npos, "wc pipe");
        shell.command("CAT NOTE | GREP HELLO > MATCH");
        check(shell.file("/MATCH") == "HELLO WORLD\r", "grep match");
        shell.command("CAT NOTE | GREP MISSING > NONE");
        check(shell.file("/NONE").empty(), "grep no matches is successful empty output");
        shell.command("CAT NOTE | GREP HELLO WORLD | WC > COUNTS");
        check(shell.file("/COUNTS") == "001 002 012\r", "multiword pattern and chained wc");
        shell.command("WRITE EMPTY");
        check(shell.command("WC EMPTY").find("\r000 000 000\r") != std::string::npos, "empty wc");
        check(shell.command("GREP HELLO").find("ERROR 3") != std::string::npos, "grep requires pipe");
        check(shell.command("ECHO HELLO | GREP").find("ERROR 3") != std::string::npos, "grep requires pattern");
        check(shell.command("WC").find("ERROR 3") != std::string::npos, "wc requires file or pipe");
        check(shell.command("WC MISSING").find("ERROR 1") != std::string::npos, "wc missing file");
        const auto install = [&](const std::string &bytes)
        {
            const auto view = apple1::inspect_ramfs(shell.machine);
            for (const auto &file : view.files)
                if (file.path == "/NOTE")
                {
                    const auto entry = 0xa000 + file.parent_slot * 256 + file.slot % 16 * 16;
                    shell.machine.write(entry + 8, bytes.size());
                    for (size_t index = 0; index < bytes.size(); ++index)
                        shell.machine.write(0xa000 + file.data_slot * 256 + index, bytes[index]);
                    return;
                }
            throw std::runtime_error("filter fixture missing");
        };
        install("ONE\r\nTWO\nTHREE\rLAST");
        check(shell.command("WC NOTE").find("\r003 004 019\r") != std::string::npos, "mixed line endings wc");
        shell.command("CAT NOTE | GREP O > MATCH");
        check(shell.file("/MATCH") == "ONE\r\nTWO\n", "grep preserves CRLF and LF");
        shell.command("CAT NOTE | GREP LAST > MATCH");
        check(shell.file("/MATCH") == "LAST", "grep preserves unterminated final line");
        install(std::string("A\0B\tC\r", 6));
        check(shell.command("WC NOTE").find("\r001 002 006\r") != std::string::npos, "wc counts NUL and tab");
        shell.command("CAT NOTE | GREP B > MATCH");
        check(shell.file("/MATCH") == std::string("A\0B\tC\r", 6), "grep preserves binary bytes");
        install(std::string(240, 'A'));
        shell.command("CAT NOTE | GREP A > MATCH");
        check(shell.file("/MATCH") == std::string(240, 'A'), "maximum line grep");
        check(shell.command("WC NOTE").find("\r000 001 240\r") != std::string::npos, "maximum wc");
        shell.command("CAT NOTE | GREP AA > MATCH");
        check(shell.file("/MATCH").size() == 240, "overlapping literal match emits once");
        install("A\r\n\nB\r\rC");
        check(shell.command("WC NOTE").find("\r004 003 008\r") != std::string::npos, "blank lines and adjacent delimiters");
        shell.command("CAT NOTE | GREP AB > MATCH");
        check(shell.file("/MATCH").empty(), "grep never matches across line boundaries");
        install("AA\rBB\nAA");
        shell.command("CAT NOTE | GREP AA > MATCH");
        check(shell.file("/MATCH") == "AA\rAA", "grep selects multiple lines without adding terminator");
        shell.command("CAT EMPTY | GREP A | WC > COUNTS");
        check(shell.file("/COUNTS") == "000 000 000\r", "empty filter chain");
        shell.command("FORMAT YES");
        const std::string chunk(199, 'A');
        for (unsigned index = 0; index < 5; ++index)
            shell.command("ECHO " + chunk + " >> BIG");
        shell.command("ECHO " + std::string(23, 'B') + " >> BIG");
        check(shell.file("/BIG").size() == 1024, "stream exact size fixture");
        shell.command("CAT BIG | CAT | WRITE COPY");
        check(shell.file("/COPY") == shell.file("/BIG"), "1 KiB multi-stage byte-exact stream");
        shell.command("GREP AAA BIG > FILTERED");
        check(shell.file("/FILTERED").size() == 1000, "file grep across many pages");
        check(shell.command("CAT BIG | WC").find("\r006 006 01024\r") != std::string::npos, "wide pipe count");
        shell.command("ECHO " + std::string(199, 'X') + " > LONG");
        shell.command("ECHO " + std::string(199, 'Y') + " >> LONG");
        shell.command("WRITE RAW " + std::string(220, 'Z'));
        shell.command("CAT RAW >> RAW");
        shell.command("ECHO END >> RAW");
        shell.command("GREP ZZZ RAW > MATCH");
        check(shell.file("/MATCH") == shell.file("/RAW"), "grep line over 256 bytes");
        shell.command("ECHO EXTRA >> BIG");
        const auto before_copy = shell.file("/COPY");
        check(shell.command("CAT BIG | WRITE COPY").find("ERROR 8") != std::string::npos,
              "capture over 1 KiB fails");
        check(shell.file("/COPY") == before_copy, "capture overflow preserves destination");
    }
    void workflow()
    {
        Shell shell;
        check(shell.output.find("A1OS\rRAMFS FORMATTED") != std::string::npos, "boot");
        check(shell.output.ends_with("\r/> "), "root path prompt");
        const auto help = shell.command("HELP");
        check(help.find("\rRM FILE - REMOVE FILE\r") != std::string::npos, "help command description");
        check(help.find("\rMKDIR PATH - CREATE DIRECTORY\r") != std::string::npos, "help directories");
        check(help.find("\rWC [FILE] - COUNT LINES WORDS BYTES\r") != std::string::npos, "help filters");
        size_t help_start = help.find('\r') + 1;
        while (help_start < help.size())
        {
            const auto end = help.find('\r', help_start);
            if (end == std::string::npos)
                break;
            check(end - help_start < 40, "help fits 40-column display");
            help_start = end + 1;
        }
        shell.command("WRITE GUIDE SAFE");
        check(shell.command("HELP > GUIDE").find("ERROR") == std::string::npos, "help redirect fits expanded capture");
        check(shell.file("/GUIDE").find("RM FILE - REMOVE FILE\r") != std::string::npos, "redirected full help");
        shell.command("RM GUIDE");
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
        check(shell.command("ECHO X >>> NOTE").find("SYNTAX ERROR") != std::string::npos, "unsupported syntax");
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
        transfers();
        filters();
        demo_and_editor();
        append_redirection();
        large_files();
        std::cout << "Directory shell and stream integration passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}