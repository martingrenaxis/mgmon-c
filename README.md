# MG Monitor (mgmon)

An interactive monitor/debugger for the Motorola 6809, with a full CPU
simulator, a one-line assembler, a disassembler, and tools for reading and
writing FLEX 6809 `.dsk` disk images.

## History

MG monitor was originally written in 6809 assembly in 1981, for a
home-built 6809 board. Martin Gren added a 6809 simulator
to it in 1990 for DOS. This repository is a C port of that assembly monitor,
extended with the simulator, assembler, disassembler, and FLEX disk-image
tooling. It still talks to the real 1981-era hardware over USB-serial
(`term`, `s1port`, `sendfile`) as well as running everything standalone in
simulation.

`first-working/` is a frozen earlier snapshot of the whole project, kept
for reference/rollback - the live code is the top-level `.c`/`.h` files.

## Build & run

```sh
make            # builds ./mgmon from mgmon.c asm09.c dis09.c sim09.c
make clean      # removes the mgmon binary
./mgmon         # run the interactive monitor
```

No external dependencies beyond a C99 compiler and a POSIX-ish terminal
(raw-mode line editing, `termios`, `IOKit` serial APIs - this has been
built and used on macOS). Type `help` at the `Mgmon>` prompt any time; it's
kept in sync with this document. `help s1`, `help flex` and `help 6809`
print focused slices of the same reference.

## Quick start

```
Mgmon
program 2000
86        <- LDA #$05
05
05         <- break (stop the simulator cleanly)
.

or short 
p.2000 

run 2000
Running from 2000
reg
```

Or write it as assembly instead of poking bytes by hand:

```
Mgmon
ass 2000
lda #$05
break
*
run 2000
```

## Command reference

All commands can be abbreviated to any unambiguous prefix, and a literal
`.` in the typed command stops the match early - `l.1000` means `list
1000`. Addresses and values are always hex. Where a command takes a
`<start> <end>` range, **the end address is exclusive** unless noted.

### Memory

| Command | Syntax | What it does |
|---|---|---|
| `list` | `list <start> [end] [width]` | Hex+ASCII dump. Omit `[end]` to page one line at a time (any key = next line, CR = stop). |
| `fill` | `fill <start> <end> <byte>` | Fill a range with one byte value. |
| `move` | `move <start> <end> <to>` | Copy a memory block. |
| `modify` | `modify <addr> <byte>` | Write one byte. |
| `check` | `check <start> <end>` | Compute a checksum of a range. |
| `verify` | `verify <start> <end> <addr2>` | Compare two memory regions. |
| `find` | `find <pattern@addr> <start>` | Search memory for a byte pattern (pattern = a count byte followed by the bytes to match). |

### Files

| Command | Syntax | What it does |
|---|---|---|
| `load` | `load <addr> <file>` | Load a raw binary file to an address. |
| `save` | `save <start> <end> <file>` | Save a memory range as a raw binary file. |
| `s1load` | `s1load <file>` | Load a Motorola S1-record file. |
| `s1send` | `s1send <start> <end> [name]` | Send memory as S1 records to stdout. |
| `s1save` | `s1save <start> <end> <file>` | Save a memory range as an S1-record file. |
| `exec` | `exec <file>` | Run a file of mgmon commands, one per line. Blank lines and lines starting with `#`, `;` or `*` are comments. |

### FLEX 6809 disk images

One `.dsk` image is "open" at a time (remembered across commands, not
passed as an argument each time):

| Command | Syntax | What it does |
|---|---|---|
| `dskfile` | `dskfile [file.dsk]` | Open a FLEX disk image, or show the currently open one. |
| `fcat` | `fcat` | List the directory of the open image. |
| `flist` | `flist <name.ext>` | List a FLEX text file as text. |
| `fget` | `fget <name.ext> [addr]` | Load a FLEX file into memory. |
| `flex2file` | `flex2file <name.ext> <hostfile>` | Copy a FLEX text file out to a host file. |
| `fsave` | `fsave <start> <end> <name.ext>` | Save a memory range as a new FLEX file. |
| `flexinject` | `flexinject <file>` | Import a host text file as a new FLEX file (FLEX name/ext derived from the host filename, e.g. `hello.txt` -> `HELLO.TXT`). |
| `fdelete` | `fdelete <name.ext>` | Delete a file from the open image. |
| `frename` | `frename <old.ext> <new.ext>` | Rename a file in the open image. |
| `sector` | `sector [track sector]` | Hex+ASCII dump of one disk sector, or the next one. Also copies that sector into `mem[0x00-0xFF]`. |
| `putsector` | `putsector [track sector]` | Write `mem[0x00-0xFF]` back as a raw sector (asks y/n - unlike the other FLEX writes, this isn't guided by the directory/free-chain structure). |
| `debug` | `debug [on\|off]` | Trace every FLEX disk write (track/sector + first 4 bytes). On by default; no argument reports the current setting. |

### Disassembler / one-line assembler

| Command | Syntax | What it does |
|---|---|---|
| `dis` | `dis <addr>` | Disassemble from an address. Space = next instruction, any other key = stop. |
| `ass` | `ass <addr>` | Interactive one-line assembler (`$` for hex in operands). Enter an instruction per line; `-`/`+` step back/forward one address, `*` stops. |

### 6809 simulator

It runs a 6809 program in memory. Besides the real 6809 instruction set,
a handful of otherwise-illegal/undefined opcodes are repurposed as
pseudo-instructions purely for a simulated program to talk to the host
(console I/O, timing, file access) - they have no meaning on real 6809/6309
hardware, and only exist because mgmon isn't running on the original board.
Use the mnemonic when writing them with `ass`; the opcode is what `dis` and
`modify` show/take:

| Opcode | Mnemonic | What it does |
|---|---|---|
| `$01` | `prtacc` | Print the character in A to the console (through the ADM3A terminal emulation, so escape sequences like clear-screen work). |
| `$02` | `getacc` | Read one character from the console into A (blocking). |
| `$05` | `break` | Stop the running program and return control to the monitor. |
| `$0B` | `delay1ms` | Delay about 1 millisecond (e.g. to pace serial output). |
| `$14` | `sensekey` | Non-blocking keyboard check: A = key code, or 0 if none waiting. |
| `$15` | `load` | Load a host file into memory. `X` = filename (ASCII, dot included, e.g. `"TEST.BIN"`, ends at any byte `< $21`). `Y` = start address. Reads the whole file; `Y` ends up pointing at the last byte written. |
| `$18` | `save` | Save memory to a host file. `X` = filename (same rule as `load`). `Y`/`U` = start/end address (inclusive). |

`load`/`save` here are the two CPU opcodes added alongside the `DIVD`/`DIVQ`
work below - **not** the monitor's own `load`/`save` commands from the
Files section above, which do the same kind of thing but are typed at the
`Mgmon>` prompt instead of executed by a simulated program.

| Command | Syntax | What it does |
|---|---|---|
| `reg` | `reg` | Show all registers. |
| `set` | `set <r> <val>` | Set a register: `p x y u s a b d c e f w` (one letter). `pc`/`cc` are also accepted as two-letter aliases for `p`/`c`. `e`/`f`/`w` are the 6309 E/F/W registers (see below). |
| `reset` | `reset` | Reset registers cold: `X=Y=U=0`, `S=$7000`, `A=B=CC=DP=0`, `PC` loaded from the reset vector at `$FFFE`. |
| `run` | `run [addr]` | Run from the current PC, or from `[addr]` if given (sets PC and prints `Running from XXXX` first). ESC breaks a running program. |
| `mode` | `mode <options>` | Set simulator options; `mode h` prints the full switch list. `s`=single-step, `r`=show registers each step, `i`=show the current instruction each step, `p <addr>`/`q <addr>`=two independent PC breakpoints, `g <n>`=run for `n` instructions then stop, `t`=trace flag (display only), `l`=assembler-output-format flag. |

Registers: `A`/`B` (or `D` combined), `X`/`Y`/`U`/`S`, `PC`, `DP`, `CC`
(condition codes), plus the 6309 `E`/`F` (or `W` combined) added for
`DIVQ` below.

**Known limitations:** `SWI`/`SWI2`/`SWI3`, `RTI`, `CWAI` and `SYNC` are
not implemented - executing one prints a message and continues rather
than doing anything.

### 6309 extras

This is not a full 6309 core - just one real 6309 instruction pair, plus
the host-I/O pseudo-ops documented in the "6809 simulator" section above
(`save`/`load` in particular were added alongside this work, since `DIVQ`
needed a `W` register anyway):

- **`DIVD`/`DIVQ`** - signed 16-bit÷8-bit and 32-bit÷16-bit divide, in the
  assembler, disassembler and simulator, all four addressing modes
  (immediate/direct/indexed/extended). `DIVD`: quotient -> B, remainder ->
  A. `DIVQ` (dividend/result in `Q` = `D:W`, a 32-bit register formed from
  `A:B:E:F`): quotient -> W, remainder -> D. Condition-code and overflow
  behavior (including a couple of genuine hardware quirks around the
  signed-negation boundary) was ported from a hardware-fuzzed reference
  implementation, not the informal Hitachi documentation floating around
  online - that document actually gets `DIVD`'s register placement wrong.

### Program info

| Command | Syntax | What it does |
|---|---|---|
| `info` | `info` | List all available commands. |
| `program` | `program <addr>` | An old-style interactive byte-at-a-time memory editor (from the original EPROM-programmer workflow): `+`/`-` move up/down, type hex bytes to write them, `"text"` writes a string, `.` exits. |
| `peps` | `peps` | PEPS programmer interface - currently a stub that does nothing. |
| `sys` | `sys <shell command>` | Execute a shell command (temporarily restores cooked terminal mode). |
| `exit` | `exit` | Quit mgmon. |

### Serial terminal (real hardware)

For talking to an actual 6809 board over USB-serial:

| Command | Syntax | What it does |
|---|---|---|
| `term` | `term [baud] [break-byte-hex]` | Connect to a USB-serial device, 8N1. Lists `/dev/cu.usbserial-*`/`cu.usbmodem*` devices to pick from. ESC returns to the monitor by default; e.g. `term 03` breaks on Ctrl-C instead. |
| `s1port` | `s1port <start> <end> [baud]` | Send memory as bare S1 records (no S0/S9) to a USB-serial device, 1ms delay between characters, sends `q` at the end and shows the board's reply. |
| `sendfile` | `sendfile <file> [baud]` | Copy a file straight to a USB-serial device as raw bytes (no S-record framing), same pacing/`q`/reply behavior as `s1port`. |

Baud rates: `2400 9600` (default) `19200 28800 38400 76800 153600`.

If a transfer to real hardware silently does nothing, check that the
board's own monitor is actually in a state ready to receive (e.g. it has
been told `s1load` at its own prompt) - `s1port`/`sendfile` can't make a
remote board start listening on its own.

### Line editing

The command prompt has Emacs-style line editing with history (persisted
across sessions to `.mgmoncommands`):

- `Up`/`Ctrl-P`, `Down`/`Ctrl-N` - previous/next command in history
- `Left`/`Ctrl-B`, `Right`/`Ctrl-F` - move back/forward one character
- `Home`/`Ctrl-A`, `End`/`Ctrl-E` - jump to start/end of line
- `Ctrl-K` - kill to end of line
- `Backspace`/`Delete` - delete a character
- `Ctrl-R` - reverse search through history (type to search, Enter to run)

## Project layout

| File | Purpose |
|---|---|
| `mgmon.c` | The monitor shell: command dispatch, line editor/history, memory/file commands, FLEX disk-image support, serial terminal, `help`. |
| `sim09.c` | The 6809 (+ a few 6309 opcodes) instruction interpreter. |
| `asm09.c` | The one-line assembler used by `ass`. |
| `dis09.c` | The disassembler used by `dis`. |
| `mgmon.h`, `typedef.h`, `asm09.h` | Shared declarations and type aliases. |
| `s1.s09` | A sample 6809 assembly program (an S1-record receiver) meant to run on real 6809 hardware, not through this monitor. |
| `system.dsk` | A sample FLEX disk image for exercising the `dskfile`/`fcat`/etc. commands. |
| `.mgmoncommands` | Persisted command-line history (created on first run). |
| `first-working/` | Frozen earlier snapshot of the project, kept for reference/rollback - not the live code. |

## Notes for anyone extending this

- New monitor commands go at the **end** of the `commands[]` array in
  `mgmon.c` - it's matched in array order by prefix, so inserting a
  command earlier can silently change what an existing abbreviation
  resolves to, and a new name that's a superstring of an earlier one can
  become unreachable.
- The simulator, assembler and disassembler all share the same opcode
  encoding conventions (`instruc[]`/`instadrm[]` in `sim09.c`); adding a
  new pseudo-op or 6309 extra means touching all three (plus, usually, the
  one-line assembler's mnemonic table in `asm09.c`) to keep them in sync -
  see `DIVD`/`DIVQ` and `save`/`load` above for the pattern.
- There's no automated test suite. Verify changes by hand: build, poke a
  short test program into memory (or assemble one with `ass`), `run` it,
  and check `reg`/`list` against what you expect.
