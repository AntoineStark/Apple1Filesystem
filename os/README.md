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
| `HELP` | Show one command per line with syntax and a short description |
| `ECHO TEXT` | Print text |
| `MEM` | Report the fixed expansion RAM/filesystem budget |
| `LS [PATH]` | List directory; sizes over 255 bytes use five decimal digits |
| `MKDIR PATH` | Create one directory; parent must exist |
| `RMDIR PATH` | Remove an empty directory, never root or the current directory |
| `CD [PATH]` | Change directory; no argument selects root |
| `PWD` | Print the absolute current-directory path |
| `WRITE NAME TEXT` | Create/replace a file with the remaining line's text |
| `WRITE NAME` | Create/truncate an empty file |
| `CAT NAME` | Print file contents followed by a carriage return |
| `CAT` | Copy piped input to output |
| `COMMAND > NAME` | Create/replace NAME with successful command output |
| `COMMAND >> NAME` | Append successful command output, creating NAME if absent |
| `COMMAND | WRITE NAME` | Save piped bytes to NAME |
| `RM NAME` | Delete a file |
| `CP SOURCE DEST` | Copy a file; create or overwrite the destination |
| `MV SOURCE DEST` | Move/rename a file; create or overwrite the destination |
| `WC [PATH]` | Count lines, words, bytes from a file or piped input |
| `COMMAND | GREP TEXT` | Emit lines containing literal TEXT |
| `GREP TEXT FILE` | Filter a named file by a single-token literal pattern |
| `FORMAT` | Display confirmation syntax without changing files |
| `FORMAT YES` | Erase and initialize the entire RAM filesystem |

Example: `WRITE NOTE HELLO WORLD`, then `LS` and `CAT NOTE`. Open **Debug > Files**
in the GUI to inspect the same RAM directory and select text/hex contents.
Paths may be absolute (`/DOCS/NOTE`) or relative (`../NOTE`, `./NOTE`). Each
component has at most eight characters; the shell accepts paths up to 95 bytes.
Repeated separators are accepted; a trailing slash requires a directory.
The special components `.` and `..` mean current and parent directory; root's
parent is root. Reset sets the current directory to root, without deleting files.
The shell prompt displays the absolute working directory before `>`, such as
`/DOCS/SUB> `. An invalid filesystem uses `?> ` so `FORMAT YES` remains available.
The GUI shows the full directory tree in one always-expanded list, independently
of the shell's working directory. Select any file for text/hex contents.

```text
MKDIR DOCS
CD DOCS
ECHO HELLO > NOTE
MKDIR SUB
CAT NOTE | WRITE SUB/COPY
LS SUB
PWD
CD /
CAT DOCS/SUB/COPY
```

Input is uppercased. Return executes, Backspace/underscore erases one character
from the line, and Escape or Ctrl-C cancels the line. The hardware display cannot
erase characters already printed, so an underscore is echoed as the erase mark.
The editor accepts at most 239 characters per command line. An overlong line is
discarded through Return, never executed as a truncated command. `WRITE` stores
the text after the name and separating spaces, without a trailing newline; its
maximum payload is less than 240 because the command and name share the line
buffer. Empty lines are harmless. No quoting, scripts, append,
or external program loading is implemented in this stage. `MEM` is a static
profile summary, not a dynamic allocator report. `CAT` is intended for text;
the debugger's Hex tab can inspect binary data.

### Streams and Pipelines

```text
ECHO HELLO WORLD > NOTE
CAT NOTE | WRITE COPY
CAT NOTE | CAT | CAT > THIRD
LS > COMMANDS
```

Spaces around `>` and `|` are optional. Up to eight sequential stages are allowed.
The Apple-1 character display renders the `|` glyph as a backslash, but the
keyboard/parser still receives the pipe character correctly.
Each intermediate stream is buffered in RAM, not run concurrently. `CAT` without
a filename consumes its piped input; `WRITE NAME` in the final stage saves that
input without adding characters. `ECHO`, `HELP`, `MEM`, and `LS` produce output;
they do not read piped input. `CAT NAME` reads the named file rather than stdin.

ECHO includes a carriage return in its output, which is also stored by `>`.
CAT copies file bytes exactly when captured for a pipe or redirection, including
NUL bytes. Direct-to-screen `CAT NAME` retains its extra display newline for
compatibility. Stream reads use a length and position, not a NUL terminator.

The whole line is parsed and command names/arguments checked before executing
any stage. Unsupported quoting, `<`, `>>>`, empty stages, duplicate redirections,
and redirection before a pipe are rejected. There is one optional `> NAME` or `>> NAME` at
the end. `RM`, `FORMAT`, `MKDIR`, `RMDIR`, and `CD` are standalone only. `WRITE` is allowed only as the
last stage without `>`; in a pipeline it takes only a filename, not literal text.
Literal `WRITE NAME TEXT` remains available as a standalone convenience command.

Intermediate streams and redirected output are limited to 1024 bytes. Overflow
prints `ERROR 8`, and the destination is unchanged. Redirection commits only
after command success; errors remain on the screen, never inside the captured
file. Missing source files, invalid commands, and full-directory errors likewise
leave an existing destination intact. Syntax errors print `SYNTAX ERROR`.
This is not a full POSIX shell: there are no concurrent processes, quoting,
input redirection, exit-status variables, or general rollback of commands.

### Append Redirection

```text
ECHO FIRST LINE > NOTES
ECHO SECOND LINE >> NOTES
CAT NOTES | GREP SECOND >> MATCHES
```

`>>` appends exact command output to an existing file or creates a missing file.
It adds no separator by itself: ECHO supplies its own carriage return, while CAT
in a pipeline/redirection supplies exact file bytes. Paths and spacing follow
the same rules as `>`. The producer completes before the old contents are read,
so `CAT FILE >> FILE` appends a snapshot once, subject to the capacity limit.
Zero output leaves an existing file unchanged and creates an empty missing file.
The shell's combined size must not exceed 4096 bytes; `ERROR 8` rejects overflow without
changing the destination. Producer errors, invalid metadata, directory targets,
missing parents, or insufficient space likewise do not alter existing contents.
Multi-block storage uses version 3 and requires explicit reformatting of old volumes.

Filesystem errors print `ERROR N`, using the API codes below. Unknown commands
print `UNKNOWN COMMAND`. A null filesystem is formatted on boot only when all
12288 bytes are zero. Nonempty invalid metadata is preserved with a warning;
`FORMAT YES` is the explicit recovery action. Reset preserves files. Power cycle
clears RAM and therefore boots an empty filesystem again. Formatting and `RM`
have no undo.

HELP's full annotated output fits the 1 KiB captured-stream limit. Both
`HELP | GREP RM` and `HELP > COMMANDS` work. Every help line fits the 40-column display.

## Text Filters

```text
WC /DOCS/NOTE
CAT /DOCS/NOTE | WC
LS | GREP NOTE
CAT /DOCS/NOTE | GREP HELLO > /DOCS/MATCHES
CAT /DOCS/NOTE | GREP HELLO WORLD | WC
```

WC prints three zero-padded decimal counts, in **lines words bytes** order, for
example `001 002 012`. Supply a filename or pipe; bare standalone WC is an error.
Lines count CR or LF terminators, with CRLF counted once. A final unterminated
line does not add a line-ending count. Words are runs separated by ASCII space,
tab, LF, vertical tab, form feed, or CR. All bytes, including NUL and both bytes
of CRLF, contribute to the byte count. Counts are 16-bit; values through 255
use three digits and larger values use five. File inputs support 4 KiB and pipe
inputs support 1 KiB. WC accepts no option flags or multiple files.

GREP accepts either `GREP TEXT FILE` or piped input with `COMMAND | GREP TEXT`.
Standalone form takes a single-token literal pattern and exactly one filename;
piped form treats all remaining text as the pattern, including spaces. File
inputs support 4 KiB; intermediate/captured output remains capped at 1 KiB.
There are no regexes, quoting, or flags. Matching is case-sensitive, although shell typing
is uppercased. CR, LF and CRLF delimit lines. Each matching line is emitted once,
preserving its original terminator or lack of one. NUL bytes do not terminate
input. A no-match result succeeds with empty output, allowing redirection to
create an empty file. Both commands use the same streams as CAT and WRITE and
can appear in multi-stage pipelines and redirect their output.

## Copy and Move

```text
CP NOTE BACKUP
CP NOTE DOCS
MV BACKUP DOCS/RENAMED
MV DOCS/RENAMED .
```

If DEST is an existing directory, the source file's basename is retained inside
it. Otherwise DEST names the output file, which is created or overwritten without
confirmation. Both paths accept absolute/relative forms, including `.` and `..`.
Parent directories must already exist, and the composed destination must fit the
95-byte path limit. An existing directory at the resulting file path is an error,
not an overwrite. Same-file operations, even through alternate path spellings,
are successful no-ops.

CP copies exact binary contents through a 4096-byte buffer. MV changes directory
metadata and retains the source data slot, so a move or rename does not require
a free data slot. Overwriting with MV releases the previous target's data slot.
A move into a full directory needs a free entry unless replacing an existing
file. Errors leave the source intact. Neither command copies or moves directory
sources recursively; those return `ERROR 9`. CP and MV require exactly two paths
and run standalone, not inside pipelines or with output redirection. There is
no undo, journaling, or protection against reset during the metadata write.

## OS Memory Layout

The ROM uses the existing expansion jumper profile, with motherboard memory
disabled. The filesystem entry table stays at `$2000`; shell code follows it.
Reset vectors at CPU `$FFFA-$FFFF` occupy physical ROM offsets `$7FFA-$7FFF`.
The shell initializes the stack and PIA itself; it does not call WozMon or BASIC.
ROM offsets unused by the OS are filled with `$FF`.

| CPU RAM | Reservation |
| --- | --- |
| `$0020-$003D` | Shell pointers, parser and stream state |
| `$0040-$005E` | Filesystem API, working directory, wide size and scratch |
| `$0060-$006F` | Wide I/O, stream counts and filter pointers |
| `$0100-$01FF` | 6502 stack |
| `$0300-$03EF` | Command line and terminator |
| `$0400-$045F` | Path and terminator |
| `$0500-$05EF` | Readback buffer |
| `$0700-$0717` | Up to eight stage begin/end offsets |
| `$0720-$077F` | Redirection destination path |
| `$6000-$63FF` | First 1 KiB pipeline buffer |
| `$6400-$67FF` | Second 1 KiB pipeline buffer |
| `$0A00-$0A2F` | Mount allocation-reference scratch |
| `$0A40-$0A48` | Path-component scratch |
| `$0B00-$0B5F` | Copy/move source path |
| `$0B60-$0BBF` | Copy/move destination path |
| `$0BC0-$0BC8` | Source basename and terminator |
| `$0BD8-$0BDA` | Transfer source entry and operation flag |
| `$0BE0-$0BE3` | Filesystem move entry and destination pointer scratch |
| `$0C00-$0C08`, `$0C20-$0C28` | WC/GREP counters and line/match scratch |
| `$0E00-$0EEF` | GREP literal pattern |
| `$0C10-$0C15` | Wide append size and decimal listing scratch |
| `$0D00-$0D2F` | Temporary file allocation plan |
| `$1000-$1FFF` | Shared 4 KiB large-file shell buffer |
| `$A000-$CFFF` | 12 KiB RAM filesystem |

Other writable RAM is left available for later program loading. This is a
single-tasking system with no memory protection. Keyboard/display polling uses
`$D010-$D013`, as on the original hardware.

## Deliberately Small Limits

- Hierarchical directories, at most 16 entries each and 15 directory levels below root.
- Files contain chains of 256-byte data blocks with 16-bit lengths. Up to 46
	blocks (11776 bytes) fit in an otherwise empty volume. Pipeline buffers remain
	1024 bytes. Direct CAT, CP, WC, GREP and append support files through 4096 bytes;
	larger files return error 4. MV and RM operate on entire chains without buffering.
- Each path component is 1 to 8 characters from `A-Z`, `0-9`, `.`, `_`, and `-`;
	`.` and `..` are reserved navigation components, never stored names.
- The library is case-sensitive; shell input is uppercased.
- Entries distinguish files from directories; there are no timestamps,
	permissions or directory-block chaining yet. File blocks are chained.
- Header and root reserve two slots; the other 46 are shared across the tree.
	Each subdirectory consumes one slot; files consume as many data slots as needed.
	Empty files need only
	their directory entry and no data slot. Deletion frees the slot for reuse.
- Overwrite replaces a file's contents and length, including truncation to zero.
- Reset retains files. Power cycle loses them. Shell boot initializes zeroed RAM;
	formatting existing/nonempty storage requires `FORMAT YES`.

The final data block may have unused bytes. The writer plans allocation before
changing file contents: insufficient space leaves an existing file unchanged.
Shrinking frees tail blocks; overwrite can reuse its own blocks on a full volume.
This is not crash-atomic: reset during a write can leave inconsistent metadata.

## Memory Format

The existing expansion profile already maps `$A000-$CFFF` to card RAM. Reserve
this entire 12 KiB region for the filesystem; do not let shell/program buffers
use it. Because the card uses `address & $7FFF`, this is physical RAM offsets
`$2000-$4FFF`. The hardware mapping is unchanged: 24 KiB of distinct RAM is
currently visible, leaving 12 KiB outside the filesystem for other uses. The
physical card has 32 KiB of RAM; exposing all of it requires a separate jumper
profile, which this change does not add.

Format version 3 is intentionally incompatible with versions 1 and 2. Old RAM contents
are never migrated or auto-erased. Back up any wanted text and issue `FORMAT YES`
to start a new volume. With zeroed RAM, boot formats version 3 automatically.

| Address | Contents |
| --- | --- |
| `$A000-$A003` | ASCII signature `A1FS` |
| `$A004` | Format version 3 |
| `$A010-$A03F` | 48 allocation types: 0 free, 1 file data, 2 directory, 3 header |
| `$A050-$A07F` | Parent slot numbers for directory slots; root's parent is 1 |
| `$A090-$A0BF` | Next block per slot; zero terminates a file chain |
| `$A100-$A1FF` | Fixed root-directory slot, number 1 |
| `$A200-$CFFF` | 46 shared slots, numbers 2 through 47 |

Slot address is `$A000 + slot * $100`; slot 0 is the header. Each directory
contains sixteen 16-byte entries, not file contents:

| Slot offset | Contents |
| --- | --- |
| `$00-$07` | Filename, padded with zero bytes; first byte zero means unused |
| `$08` | File length low byte; zero for directories |
| `$09` | Type: 1 file, 2 directory |
| `$0A` | Associated storage slot; 0 means an empty file without data storage |
| `$0B` | File length high byte; zero for directories |
| `$0C-$0F` | Reserved |

Nonempty files store bytes at offset 0 of each associated slot, with all 256
bytes usable per data block. The header's NEXT table links blocks. The last link
must be zero, and chain length must exactly match the rounded-up file length.
Directory names and metadata do not consume file bytes. File count depends on directory and data-slot use, not a
fixed global 15-file limit. A full directory and a full slot pool both report
error 2. Directory growth/chaining is deferred. Delete frees the full chain and marks the entry unused
and releases storage; it does not securely erase old data. Overwrite
does not clear bytes past the new length. Readers must use the recorded length.

Mount checks the signature, version, occupied filenames and padding, duplicate
names within each directory, lengths, types, ownership, allocation references,
parent links, reachability and the depth bound. Shared/orphaned slots and parent
cycles are rejected. It ignores unused-slot contents and
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
| `$2003` | Format | Clear all 12 KiB, initialize header/root, set CWD to 1 |
| `$2006` | Find | NAME; returns SLOT and ENTRY |
| `$2009` | Put | NAME, DATA, LENGTH; create or replace; returns SLOT and ENTRY |
| `$200C` | Get | NAME, DATA; copy file; returns LENGTH, SLOT and ENTRY |
| `$200F` | Remove | NAME; free the matching slot |
| `$2012` | Info | A = entry index 0..15 in CWD; returns ENTRY, LENGTH, SLOT |
| `$2015` | Mkdir | NAME path; create one directory |
| `$2018` | Rmdir | NAME path; remove empty non-root directory |
| `$201B` | Chdir | NAME path; update CWD on success |
| `$201E` | Pwd | DATA points to 240-byte buffer; returns path and LENGTH |
| `$2021` | Move file | NAME source, DATA destination file path; metadata rename/overwrite |
| `$2024` | Put wide | NAME, DATA, LENGTH low byte, `$57` high byte; full-file create/replace |
| `$2027` | Get wide | NAME, DATA destination; returns LENGTH and `$57` |

Every operation except Format validates the filesystem before accessing files.
Move file takes a fully resolved destination file path, not directory-target
syntax (the shell appends the basename). It clobbers NAME to the destination;
its four-byte scratch region at `$0BE0-$0BE3` must be reserved by callers.
Put also rejects an oversized length before mounting. A successful call returns
carry clear and A = 0. Failure returns carry set and an error in A:

| Code | Meaning |
| --- | --- |
| 1 | File not found or enumeration slot unused |
| 2 | Directory full or no free shared storage slot |
| 3 | Invalid input filename |
| 4 | Bounded read/write size exceeded, wide size invalid, or directory depth limit |
| 5 | Invalid/unformatted filesystem |
| 6 | Enumeration entry outside 0 to 15 |
| 9 | Wrong type, or attempt to remove root/current special path |
| 10 | Directory creation target already exists |
| 11 | Directory not empty, current, or an ancestor of current |

The caller supplies these zero-page fields:

| Address | Field |
| --- | --- |
| `$40-$41` | NAME: pointer to a null-terminated path |
| `$42-$43` | DATA: source for Put, destination for Get |
| `$44` | LENGTH: input to Put, output from Get/Info |
| `$45-$46` | ENTRY: returned slot pointer; scratch during calls |
| `$47` | SLOT: associated data/directory slot, 0 for an empty file |
| `$48-$4B`, `$4D-$56`, `$58-$5E` | Internal scratch |
| `$57` | Wide file size high byte (input Put wide, output Get/Get wide/Info) |
| `$4C` | CWD: current directory slot; initialize to 1 on reset |

A, X, Y, flags and scratch fields are clobbered. Except for Move file, NAME and DATA pointers remain
unchanged. Do not rely on returned fields after a failed call. Info's ENTRY
points directly at the eight-byte padded name, not a null-terminated string
when the name uses all eight characters. Find on `/`, `.`, or `..` can return
the directory itself with ENTRY's low byte `$FF` as a sentinel and SLOT naming
that directory. Normal entry offsets are multiples of 16, never `$FF`.

The caller must supply valid pointers: NAME needs readable storage for its
terminator within 256 readable bytes (the shell limits paths to 95); Get and Pwd
require a writable 240-byte destination. Get wide requires enough space for the
entire validated file; callers must inspect entry length before invoking it.
Put wide accepts at most 11776 bytes, subject to available blocks. Buffers must not
overlap the filesystem, zero-page workspace, or stack, and must not wrap at
`$FFFF`. The library is single-tasking and not reentrant. No raw metadata pointer
should be given to an untrusted program.

## Build and Tests

Install cc65 (`brew install cc65` on macOS), configure the usual CMake preset,
and build using CMake Tools. The `apple1-ramfs` target assembles/links the library
and emits `build/ramfs.bin` and `build/ramfs.map`. `APPLE1_OS=OFF` excludes the
assembler dependency and filesystem target for emulator-only builds.

The CTest `ramfs` suite uses `tests/ramfs_v2_tests.cpp`, loads this exact binary into emulated card ROM, calls
its entry points on the emulated 6502, and checks RAM and return flags. It covers
empty, one-byte and maximum-size binary files; overwrite/truncation; invalid
names; oversized input; missing files; enumeration; full storage; slot reuse;
duplicate names and corrupt header/name/length metadata; reset retention;
power-cycle loss; and guest-write boundaries. It does not substitute a C++
filesystem implementation for the assembly.

The CTest `shell` suite uses `tests/shell_v2_tests.cpp`, boots the complete OS ROM through its reset vector,
feeds characters through the PIA, and checks command output and guest RAM. It
also verifies viewer snapshots are read-only and reject malformed metadata.
Use `a1os.rom`, not the 4096-byte library image, when booting a frontend.
The older `ramfs_tests.cpp` and `shell_tests.cpp` preserve version-1 historical
tests; they are not registered with CTest. Version-2 coverage includes allocation
of slot 47 at `$CF00`, more than 15 files across directories, slot reuse, depth,
current-directory safety, relative/absolute paths, and corrupt parent graphs.