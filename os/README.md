# A1OS Shell and RAM Filesystem

This is guest-side NMOS 6502 assembly, not an emulator storage device or host
filesystem. A bootable shell now uses the independently tested filesystem
routines. The compilation ROM remains available separately and the card's
hardware mapping is unchanged.

## Boot and Commands

The `apple1-os` build target emits `build/a1os.rom`, a complete 32768-byte card
image, plus `build/a1os.map`. The GUI loads this image by default. For the
terminal, run `./build/apple1-terminal --rom build/a1os.rom` from the repository
root. The standard Apple-1 profile still boots WozMon, not A1OS.

| Command | Operation |
| --- | --- |
| `HELP` | List commands and basic syntax |
| `ECHO TEXT` | Print text |
| `MEM` | Report the fixed expansion RAM/filesystem budget |
| `LS` | List root filenames and three-digit decimal byte lengths |
| `WRITE NAME TEXT` | Create/replace a file with the remaining line's text |
| `WRITE NAME` | Create/truncate an empty file |
| `CAT NAME` | Print file contents followed by a carriage return |
| `RM NAME` | Delete a file |
| `FORMAT` | Display confirmation syntax without changing files |
| `FORMAT YES` | Erase and initialize the entire RAM filesystem |

Example: `WRITE NOTE HELLO WORLD`, then `LS` and `CAT NOTE`. Open **Debug > Files**
in the GUI to inspect the same RAM directory and select text/hex contents.
The root is shown as `/` by the debugger; shell filenames do not take a `/`.

Input is uppercased. Return executes, Backspace/underscore erases one character
from the line, and Escape or Ctrl-C cancels the line. The hardware display cannot
erase characters already printed, so an underscore is echoed as the erase mark.
The editor accepts at most 239 characters per command line. An overlong line is
discarded through Return, never executed as a truncated command. `WRITE` stores
the text after the name and separating spaces, without a trailing newline; its
maximum payload is less than 240 because the command and name share the line
buffer. Empty lines are harmless. No quoting, pipes, scripts, append, directories,
or external program loading is implemented in this stage. `MEM` is a static
profile summary, not a dynamic allocator report. `CAT` is intended for text;
the debugger's Hex tab can inspect binary data.

Filesystem errors print `ERROR N`, using the API codes below. Unknown commands
print `UNKNOWN COMMAND`. A null filesystem is formatted on boot only when all
4096 bytes are zero. Nonempty invalid metadata is preserved with a warning;
`FORMAT YES` is the explicit recovery action. Reset preserves files. Power cycle
clears RAM and therefore boots an empty filesystem again. Formatting and `RM`
have no undo.

## OS Memory Layout

The ROM uses the existing expansion jumper profile, with motherboard memory
disabled. The filesystem entry table stays at `$2000`; shell code follows it.
Reset vectors at CPU `$FFFA-$FFFF` occupy physical ROM offsets `$7FFA-$7FFF`.
The shell initializes the stack and PIA itself; it does not call WozMon or BASIC.
ROM offsets unused by the OS are filled with `$FF`.

| CPU RAM | Reservation |
| --- | --- |
| `$0020-$002B` | Shell pointers, line size, command/list indices |
| `$0040-$004B` | Filesystem API and scratch |
| `$0100-$01FF` | 6502 stack |
| `$0300-$03EF` | Command line and terminator |
| `$0400-$0408` | Filename and terminator |
| `$0500-$05EF` | Readback buffer |
| `$A000-$AFFF` | RAM filesystem |

Other writable RAM is left available for later program loading. This is a
single-tasking system with no memory protection. Keyboard/display polling uses
`$D010-$D013`, as on the original hardware.

## Deliberately Small Limits

- One flat directory; no paths or subdirectories.
- At most 15 files, each containing 0 to 240 bytes of arbitrary binary data.
- Names are 1 to 8 characters from `A-Z`, `0-9`, `.`, `_`, and `-`.
- The library is case-sensitive; shell input is uppercased.
- No file types, timestamps, permissions, append operation, or allocation chains.
- Each file owns one fixed slot, so deletion immediately makes that slot reusable.
- Overwrite replaces a file's contents and length, including truncation to zero.
- Reset retains files. Power cycle loses them. Shell boot initializes zeroed RAM;
	formatting existing/nonempty storage requires `FORMAT YES`.

The 240-byte file limit is intentional for the first stage. It is enough for
short notes, scripts, and tiny test programs, not substantial applications.
Unused capacity in one slot cannot be borrowed by another file.

## Memory Format

The existing expansion profile already maps `$A000-$AFFF` to card RAM. Reserve
this entire 4 KiB region for the filesystem; do not let shell/program buffers
use it. Because the card uses `address & $7FFF`, this is physical RAM offsets
`$2000-$2FFF`. CPU addresses `$2000-$2FFF` currently select ROM, not this RAM.

| Address | Contents |
| --- | --- |
| `$A000-$A003` | ASCII signature `A1FS` |
| `$A004` | Format version, currently 1 |
| `$A005-$A0FF` | Reserved; initialized to zero |
| `$A100-$AFFF` | Fifteen 256-byte slots |

Slot number is zero-based, from 0 to 14. Its address is
`$A100 + slot * $100`:

| Slot offset | Contents |
| --- | --- |
| `$00-$07` | Filename, padded with zero bytes; first byte zero means unused |
| `$08` | File length, 0 to 240 |
| `$09-$0F` | Reserved |
| `$10-$FF` | Up to 240 bytes of file contents |

Total usable file data capacity is 3600 bytes, with a maximum of 240 per file.
Delete only marks a slot unused; it does not securely erase old data. Overwrite
does not clear bytes past the new length. Readers must use the recorded length.

Mount checks the signature, version, occupied filenames and padding, duplicate
names, and all occupied-slot lengths. It ignores unused-slot contents and
reserved bytes. It never writes repairs or automatically formats unknown RAM.
There are no checksums or guarantees against interrupted writes: a recognizable
but partially written file can still mount. This is a demo filesystem, not a
power-failure-safe format.

## Routine API

`ramfs.bin` contains routines linked at `$2000` in a 4096-byte, `$FF`-padded ROM
segment. It is a test/library image, not a complete 32 KiB boot ROM. Its entry
table uses JMP instructions so callers invoke entries with JSR:

| Entry | Routine | Inputs / result |
| --- | --- | --- |
| `$2000` | Mount | Validate existing filesystem; never format |
| `$2003` | Format | Clear all 4 KiB and write signature/version |
| `$2006` | Find | NAME; returns SLOT and ENTRY |
| `$2009` | Put | NAME, DATA, LENGTH; create or replace; returns SLOT and ENTRY |
| `$200C` | Get | NAME, DATA; copy file; returns LENGTH, SLOT and ENTRY |
| `$200F` | Remove | NAME; free the matching slot |
| `$2012` | Info | A = slot index; returns NAME via ENTRY and length via LENGTH |

Every operation except Format validates the filesystem before accessing files.
Put also rejects an oversized length before mounting. A successful call returns
carry clear and A = 0. Failure returns carry set and an error in A:

| Code | Meaning |
| --- | --- |
| 1 | File not found or enumeration slot unused |
| 2 | All 15 slots occupied |
| 3 | Invalid input filename |
| 4 | Input length exceeds 240 bytes |
| 5 | Invalid/unformatted filesystem |
| 6 | Enumeration slot outside 0 to 14 |

The caller supplies these zero-page fields:

| Address | Field |
| --- | --- |
| `$40-$41` | NAME: pointer to a null-terminated filename |
| `$42-$43` | DATA: source for Put, destination for Get |
| `$44` | LENGTH: input to Put, output from Get/Info |
| `$45-$46` | ENTRY: returned slot pointer; scratch during calls |
| `$47` | SLOT: returned slot index; scratch during calls |
| `$48-$4B` | Internal scratch |

A, X, Y, flags and scratch fields are clobbered. NAME and DATA pointers remain
unchanged. Do not rely on returned fields after a failed call. Info's ENTRY
points directly at the eight-byte padded name, not a null-terminated string
when the name uses all eight characters.

The caller must supply valid pointers: NAME needs readable storage for its
terminator, and Get requires a writable 240-byte destination. Buffers must not
overlap the filesystem, zero-page workspace, or stack, and must not wrap at
`$FFFF`. The library is single-tasking and not reentrant. No raw metadata pointer
should be given to an untrusted program.

## Build and Tests

Install cc65 (`brew install cc65` on macOS), configure the usual CMake preset,
and build using CMake Tools. The `apple1-ramfs` target assembles/links the library
and emits `build/ramfs.bin` and `build/ramfs.map`. `APPLE1_OS=OFF` excludes the
assembler dependency and filesystem target for emulator-only builds.

The CTest `ramfs` suite loads this exact binary into emulated card ROM, calls
its entry points on the emulated 6502, and checks RAM and return flags. It covers
empty, one-byte and maximum-size binary files; overwrite/truncation; invalid
names; oversized input; missing files; enumeration; full storage; slot reuse;
duplicate names and corrupt header/name/length metadata; reset retention;
power-cycle loss; and guest-write boundaries. It does not substitute a C++
filesystem implementation for the assembly.

The CTest `shell` suite boots the complete OS ROM through its reset vector,
feeds characters through the PIA, and checks command output and guest RAM. It
also verifies viewer snapshots are read-only and reject malformed metadata.
Use `a1os.rom`, not the 4096-byte library image, when booting a frontend.