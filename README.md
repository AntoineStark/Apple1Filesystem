# Apple-1 Emulator

A from-scratch C++20 NMOS 6502 emulator for the standard Apple-1 and the
Aberco/SiliconInsider 64 KiB ROM/RAM expansion card. Terminal and native Dear
ImGui frontends share the CPU, memory mapping, keyboard, and timed display.
It runs both the original apple1loader compilation and our bootable A1OS shell.

The GUI defaults to A1OS: a guest-side 6502 shell with a fixed-slot RAM
filesystem, reserving 12 KiB of card RAM with hierarchical directories and
multi-block files. Each directory holds up to 16 entries; file data and
subdirectories share 46 storage slots after the header and fixed root slot.
See [os/README.md](os/README.md) for commands, ROM layout, format, and limits.

## Setup

Requires CMake 3.24 or newer and a C++20 compiler. The terminal frontend uses
POSIX terminal APIs (macOS/Linux). The GUI uses SDL2, OpenGL 3.2, and Dear ImGui
docking. CMake downloads pinned GUI dependencies when needed; SDL2 can also be
provided by an installed CMake package. No Python is needed.
The default build now also includes the OS filesystem routines and requires
ca65/ld65 from cc65 (`brew install cc65` on macOS). For the emulator alone,
configure with `-DAPPLE1_OS=OFF`; that build needs no assembler.

From the repository root, acquire the checksum-verified firmware:

```sh
cmake -P cmake/FetchRoms.cmake
cmake --preset debug
```

In VS Code, select the `debug` configure preset with **CMake: Select Configure
Preset**, then use **CMake: Build**, or the **Build emulator** build task.
Use **CMake: Run Tests** to run the registered test suites.

For a build without graphical dependencies, configure with
`cmake --preset terminal`, select the `terminal` preset, and use **CMake: Build**.
That configuration produces executables in `build-terminal/` instead of `build/`.
Keep generated files in those directories, not the source directory.

ROM binaries are downloaded locally, ignored by Git, and not redistributed in
this project. The download script pins apple1loader revision
`3f69e3f35f19078f9437be7338d6b582c8d1e4e5`; see
[apple1loader](https://github.com/fstark/apple1loader) for firmware provenance.

## Run

Run from the repository root so the default relative ROM paths resolve:

```sh
./build/apple1-terminal
./build/apple1-gui
```

Both default to the expansion hardware profile. The GUI boots the generated
`a1os.rom` directly into the shell; the terminal retains its compilation-ROM
default. At the shell prompt, try:

```text
HELP
ECHO HELLO WORLD > NOTE
ECHO SECOND LINE >> NOTE
MKDIR DOCS
CAT NOTE | WRITE DOCS/COPY
CP NOTE DOCS/BACKUP
MV DOCS/BACKUP DOCS/SAVED
CD DOCS
PWD
LS
CAT COPY
CAT COPY | WC
LS | GREP COPY
CD /
```

Use **Debug > Files** in the GUI to browse directories and select
a file for text/hex contents. The viewer is read-only and validates metadata.
Reset preserves files; Power cycle clears RAM. `FORMAT YES` explicitly erases
the filesystem. Cold boot initializes it only when the entire 12 KiB region is
zero; an invalid nonempty filesystem produces a warning instead of being erased.
Version-1/2 RAM volumes are not migrated: issue `FORMAT YES` explicitly to replace
an old format. Reset retains the directory tree and returns the shell to `/`.

To select firmware explicitly:

```sh
./build/apple1-terminal --rom build/a1os.rom
./build/apple1-gui --rom roms/32KA1COMPIL.BIN
```

In an emulator-only build (`APPLE1_OS=OFF`), the GUI retains the compilation-ROM
default. With the compilation ROM, wait for the menu, then press `W` for
WozMon, `I` for Integer BASIC, `A` for the 8 KiB memory test, or `5` for C64 Maze.
In BASIC, enter `PRINT 2+3` and press Return. Press Reset to return to the menu.

To run the standard machine instead:

```sh
./build/apple1-terminal --profile standard
./build/apple1-gui --profile standard
```

This boots the original 256-byte WozMon ROM at `$FF00`. For example, enter
`0300: A9 C1 20 EF FF 4C 00 FF` followed by `0300R`; the deposited program prints
`A` and returns to WozMon. Both executables accept `--rom PATH` to override the
profile's default firmware image. Image lengths must be exactly 256 bytes for
standard or 32768 bytes for expansion.

### Terminal

Input is uppercased, Return becomes carriage return, and Backspace becomes the
Apple-1 underscore erase character. Paste and `--input` are paced to avoid
overrunning guest software. Guest software is responsible for echo.

| Host key | Action |
| --- | --- |
| Ctrl-] | Exit |
| Ctrl-R | CPU/machine reset, preserving RAM and screen |
| Ctrl-L | Clear the screen independently of reset |
| Ctrl-P | Pause/resume |

Normal execution is paced at approximately 1 MHz. `--turbo` removes host pacing
but preserves guest device timing. `--cycles N` bounds execution, allowing the
last instruction to cross the limit. `--input TEXT` supplies literal text.

```sh
./build/apple1-terminal --turbo --cycles 12000000 --input $'IPRINT 2+3\r' </dev/null
```

This console connects to the Apple-1 PIA; it is not an emulated UART or serial
expansion card. Terminal settings and signal handlers are restored on exit.

### ImGui Debugger

Click inside the emulated screen to direct keyboard input there. Other panels
include CPU registers/disassembly, step/step-over/run-to-cursor controls,
execute/read/write stop points, memory views and RAM editing, physical ROM/RAM
inspection, PIA state, bounded instruction tracing, and diagnostics.
The **Debug > Files** toggle opens the directory/file viewer, docked beside
Memory. It reports names, byte lengths, directory-entry addresses and associated
storage slots, with Text and Hex tabs for
the selected file. Invalid/unmapped filesystems are reported without modification.
The root and every directory/file appear together in an always-expanded,
indented tree. Select a file anywhere in the list to inspect its contents;
no folder navigation is needed, and selection does not change the shell's cwd.
The shell prompt includes its current absolute directory, for example
`/DOCS> ` (or `/> ` at root). Invalid filesystem state uses `?> ` for recovery.

Reset and Power cycle resume execution and return keyboard focus to the screen.
Reset preserves RAM and screen contents; both actions discard pending input.
Use Pause explicitly when inspecting a stopped machine.

The **Input Buffer** tab, docked alongside Trace and Diagnostics, shows queued
characters and the unread keyboard-device count. Keyboard input and text sent
from this tab are delivered at one character per 100 ms of emulated time,
waiting for each device character to be consumed. Paste text into the editor,
choose whether to append Return, and press Send. Clear queue discards both the
waiting characters and the device's unread character, without resetting RAM.
The preview displays carriage returns as `<CR>` and escape as `<ESC>`.

The editor starts with an A1OS demo covering the shell commands, directories,
copy/move, files, filters, pipes and redirection. It is not executed automatically.
**Load demo** restores it, replacing the editor contents but not files or queued
input. It assumes a fresh A1OS session (or no existing `/DEMO` tree) and leaves
the example files for inspection. Its `FORMAT` command only displays the
confirmation syntax; the demo never sends `FORMAT YES`.

**Send line** sends only the line containing the caret, always followed by Return,
and moves the caret to the next line. It leaves the script intact and does not
queue subsequent lines. It is disabled while earlier input is queued or latched.
Click within another line to run that line instead. **Send** retains its original
whole-editor behavior and uses the Append Return checkbox. The scripts use A1OS
commands; do not send them to the compilation-ROM menu or standard WozMon.

The memory view selector distinguishes the mapped bus from the physical chips.
Debugger reads do not acknowledge keyboard input. Pause before editing the
memory-map draft. Invalid or conflicting mappings are rejected. Apply changes,
then power cycle explicitly to start with cleared RAM and the new map.

Native rendering and boot smoke checks can also be run directly:

```sh
./build/apple1-gui --smoke-test --screenshot build/gui-shell.ppm
./build/apple1-gui --profile standard --smoke-test
./build/apple1-gui --input-smoke-test
./build/apple1-gui --profile standard --input-smoke-test
./build/apple1-gui --rom roms/32KA1COMPIL.BIN --input-smoke-test
./build/apple1-gui --demo-smoke-test
```

These finish after 180 frames and check for emulated output and a nonblank
OpenGL framebuffer. They require a graphical desktop session, not a headless
server. Screenshot files are binary PPM images.
The input smoke variant also clicks the Reset button, checks execution and
screen focus, and injects a synthetic SDL text event through the normal GUI
input path to verify nested shell file creation/readback and viewer contents, BASIC
arithmetic for the compilation ROM, or WozMon input for the standard profile.
The demo variant sends the exact preloaded editor script one line at a time,
waiting for each shell prompt, and checks its final directory tree.

## Hardware Profiles

The standard profile has 4 KiB RAM at `$0000-$0FFF`, 4 KiB RAM at
`$E000-$EFFF`, and WozMon ROM at `$FF00-$FFFF`.

The expansion contains separate 32 KiB RAM and 32 KiB ROM arrays. Each of the
16 CPU segments selects RAM, ROM, or disconnected. Both chips use
`address & $7FFF`, so selecting the same chip in segments eight apart aliases
the same physical storage. There is no software bank-select register.

The default loader-compatible map is:

| CPU addresses | Card mapping |
| --- | --- |
| `$0000-$1FFF` | RAM |
| `$2000-$5FFF` | ROM |
| `$6000-$6FFF` | RAM |
| `$7000-$9FFF` | ROM |
| `$A000-$CFFF` | RAM |
| `$D000-$DFFF` | Disconnected; motherboard PIA remains available |
| `$E000-$FFFF` | ROM |

Motherboard RAM and monitor are disabled in this profile. It exposes 24 KiB of
distinct card RAM, all 32 KiB of ROM, and a ROM alias at `$F000-$FFFF`.
Card segment D must remain disconnected. Conflicts with enabled motherboard
memory are rejected. EPROM writes are ignored; unmapped reads return `$FF`.

Keyboard/display registers are at `$D010-$D013`. The display accepts characters
at approximately one per 16.667 ms of guest time; its busy handshake is essential
for the loader's startup checks. The screen has 40 columns and 24 rows.

## Validation and Limits

CTest includes CPU arithmetic/addressing/opcode tests, mapping and PIA tests,
unmodified ROM boot and menu workflows, and an end-to-end terminal BASIC test.
ROM checks cover both profiles, WozMon deposit/execute, BASIC arithmetic,
4 KiB/8 KiB memory tests, display output, and a copied-to-RAM menu program.
The `ramfs` and `shell` suites execute the assembled OS routines on the emulated
CPU. Shell coverage includes file creation, readback, overwrite, deletion, full
storage, confirmed formatting, reset retention, corrupt filesystem preservation,
line editing/cancellation/overflow, write boundaries, and read-only viewer checks.

The independent Klaus 6502 functional suite is optional:

```sh
cmake -P cmake/FetchCpuTest.cmake
cmake --preset debug -DAPPLE1_FUNCTIONAL_ROM="$PWD/refs/6502_functional_test.bin"
```

Build with **CMake: Build** and run **CMake: Run Tests**. The added
`cpu_functional` test checks the suite's success trap at `$3469`. Its upstream
revision and SHA-256 are pinned; the binary remains outside Git. Source and
license: [Klaus2m5/6502_65C02_functional_tests](https://github.com/Klaus2m5/6502_65C02_functional_tests).

The CPU implements the 151 documented NMOS instructions and instruction cycle
counts, including decimal arithmetic and indirect-JMP wraparound. It is not a
transistor-level or cycle-by-cycle bus model. Undocumented opcodes stop with a
diagnostic. The PIA/display model implements the behavior needed by the supplied
software, not every electrical mode or address mirror of the original board.
Cassette, disk hardware, a guest UART, snapshots, ROM patch overlays, and
exhaustive GUI-interaction automation are not implemented yet. A1OS currently
has no external program loading or scripts. The shell supports
output redirection with `>` and up to eight sequential pipeline stages with `|`,
plus `>>` append redirection (up to 4 KiB combined file size in the shell),
using 1 KiB intermediate buffers. `HELP | GREP RM` and `GREP RM COMMANDS` are
supported. File-based WC/GREP accept up to 4 KiB. See [os/README.md](os/README.md) for limits.