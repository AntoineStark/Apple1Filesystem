# RAM Filesystem: First OS Component

This is guest-side NMOS 6502 assembly, not an emulator storage device or host
filesystem. The current milestone is an independently tested library, not a
bootable shell. The existing Apple-1 compilation ROM and emulator profiles
remain unchanged.

## Deliberately Small Limits

- One flat directory; no paths or subdirectories.
- At most 15 files, each containing 0 to 240 bytes of arbitrary binary data.
- Names are 1 to 8 characters from `A-Z`, `0-9`, `.`, `_`, and `-`.
- Names are case-sensitive; the future shell will handle uppercase conversion.
- No file types, timestamps, permissions, append operation, or allocation chains.
- Each file owns one fixed slot, so deletion immediately makes that slot reusable.
- Overwrite replaces a file's contents and length, including truncation to zero.
- Reset retains files. Power cycle loses them. Formatting is always explicit.

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

Next stage: bootable shell with `HELP`, `ECHO`, `LS`, `CAT`, and a simple way to
write a short file. Choose an OS ROM layout before combining the routines with
reset vectors and the shell; do not replace the compilation ROM with this
library image.