# Apple-1 Emulator

A from-scratch C++20 NMOS 6502 emulator for the standard Apple-1 and the
Aberco/SiliconInsider 64 KiB ROM/RAM expansion card. Terminal and native Dear
ImGui frontends share the CPU, memory mapping, keyboard, and timed display.
The initial goal is to run the existing apple1loader compilation unchanged.

## Setup

Requires CMake 3.24 or newer and a C++20 compiler. The terminal frontend uses
POSIX terminal APIs (macOS/Linux). The GUI uses SDL2, OpenGL 3.2, and Dear ImGui
docking. CMake downloads pinned GUI dependencies when needed; SDL2 can also be
provided by an installed CMake package. No Python or assembler is needed.

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

Both default to the expansion profile and boot the unmodified compilation ROM
through its reset vector at `$2000`. Wait for the menu, then press `W` for
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

The memory view selector distinguishes the mapped bus from the physical chips.
Debugger reads do not acknowledge keyboard input. Pause before editing the
memory-map draft. Invalid or conflicting mappings are rejected. Apply changes,
then power cycle explicitly to start with cleared RAM and the new map.

Native rendering and boot smoke checks can also be run directly:

```sh
./build/apple1-gui --smoke-test --screenshot build/gui-expansion.ppm
./build/apple1-gui --profile standard --smoke-test
```

These finish after 180 frames and check for emulated output and a nonblank
OpenGL framebuffer. They require a graphical desktop session, not a headless
server. Screenshot files are binary PPM images.

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
Cassette, disk/filesystem hardware, a guest UART, snapshots, ROM patch overlays,
and exhaustive GUI-interaction automation are not implemented yet.