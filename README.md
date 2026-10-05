# mspsim

A command-line simulator of the classic MSP430 CPU (16-bit, 64 KB address space; no MSP430X). It runs firmware built with `msp430-elf-gcc` and provides:

- all 27 instructions and seven addressing modes, with cycle counts from the MSP430x2xx family user's guide (SLAU144);
- a console UART at the MSP430G2xx USCI_A0 addresses, so programs print to stdout and read stdin;
- a stop register that ends the run with an exit status, for test programs;
- newlib's host I/O, so a program built with `msp430-elf-gcc -msim` uses `printf`, `fgets` and `exit` unchanged;
- an execution trace of instructions, register changes, and memory loads and stores;
- an interactive debugger with breakpoints, disassembly with symbols, and line editing.

The whole 64 KB is plain RAM; there are no other peripherals.

## Build

You need CMake 3.16 or newer and a C11 compiler (gcc or clang) on Linux or macOS. The top-level Makefile wraps CMake:

| Command | What it does |
|---|---|
| `make` | Configure `build/` (RelWithDebInfo) on first use, then build everything |
| `make test` | Build, then run all tests with ctest |
| `make install` | Install `mspsim` into `~/.local/bin`, or into `/usr/local/bin` if `~/.local` doesn't exist |
| `make debug` | Reconfigure `build/` for a Debug build; follow with `make` |
| `make format` | Reformat the C sources with clang-format |
| `make clean` | Remove `build/` |

Or use CMake directly:

```bash
cmake -B build
cmake --build build
ctest --test-dir build
```

## Usage

```
mspsim [options] firmware
  -b, --binary ADDR     raw binary loaded at ADDR (default: ELF or Intel HEX)
  -g, --debug           start paused in the interactive debugger
  -n, --max-cycles N    stop after N cycles
  -t, --trace           trace executed instructions, register changes, loads/stores
  -o, --trace-file F    write trace to F instead of stderr (implies -t)
  -q, --quiet           no banner/diagnostics, only UART output
  -h, --help            show this help
```

The firmware is an ELF file or an Intel HEX file; the format is detected from the contents. ELF `PT_LOAD` segments are loaded at their physical addresses, and the symbol table is read. A raw binary image needs `-b ADDR`.

Execution starts at the reset vector (0xFFFE). If the vector is erased (0xFFFF), it starts at the ELF entry point, the HEX start address, or the `-b` address.

UART output goes to stdout, and diagnostics go to stderr. So `mspsim -q fw.elf > out.txt` captures just what the program prints.

### Ending the run

The run ends when the program writes to the stop register, executes an illegal instruction, reaches the `-n` cycle limit, or turns the CPU off with no way to wake up. That last case means `CPUOFF` set with interrupts disabled, no UART receive interrupt enabled, or stdin at end of file. Unless `-q` is given, the reason and the total cycle count are printed. The exit status says which:

| Status | Meaning |
|---|---|
| value written to 0x01FE (low 8 bits) | the program stopped itself |
| value passed to `exit()` (low 8 bits) | a newlib program exited |
| 128 + signal | a newlib program called `kill()`, as `abort()` does (134) |
| 124 | cycle limit reached |
| 125 | CPU off with no wake-up source |
| 130 | interrupted with Ctrl-C |
| 132 | illegal instruction |
| 1 | the firmware could not be loaded |
| 2 | bad command line |

## Console UART

The UART is a subset of USCI_A0 at the MSP430G2xx addresses, so ordinary programs that print over the UART run unchanged:

| Address | Register | Behaviour |
|---|---|---|
| 0x0067 | `UCA0TXBUF` | A byte written here goes to stdout immediately |
| 0x0066 | `UCA0RXBUF` | Reading returns the next byte of stdin and clears `UCA0RXIFG` |
| 0x0003 | `IFG2` | Bit 1 `UCA0TXIFG` is always 1; bit 0 `UCA0RXIFG` is set while an input byte is waiting |
| 0x0001 | `IE2` | Bit 0 `UCA0RXIE` enables the receive interrupt (vector 0xFFEE) |
| 0x01FE | stop register | A byte or word write stops the simulator; the value becomes the exit status |

The other USCI registers (`UCA0CTL0`, `UCA0BR0`, …) are plain memory, so baud-rate setup works but has no effect. Stdin is read without blocking. On a terminal it is in raw mode while the program runs, so keys go straight to the UART, and Ctrl-] returns to the debugger.

A minimal C program:

```c
#define IFG2      (*(volatile unsigned char *)0x0003)
#define UCA0TXBUF (*(volatile unsigned char *)0x0067)
#define SIM_STOP  (*(volatile unsigned int *)0x01FE)

int main(void)
{
    for (const char *s = "Hello\n"; *s; s++) {
        while (!(IFG2 & 0x02))
            ;
        UCA0TXBUF = *s;
    }
    SIM_STOP = 0;
}
```

## newlib programs

`msp430-elf-gcc -msim` links newlib's simulator support (libgloss `libsim.a`) and the linker script `msp430-sim.ld`. That support does its I/O in two ways, and mspsim serves both, so such a program needs no UART code of its own:

```bash
msp430-elf-gcc -mcpu=msp430 -msim -O2 prog.c -o prog.elf
mspsim prog.elf < input.txt
```

- **TI's CIO breakpoint.** `write()` fills the buffer `__CIOBUF__` and calls `C$$IO$$`, a `nop; ret` where a debugger is meant to stop. When the ELF has both symbols, mspsim serves the request as PC reaches `C$$IO$$`, then runs the hook as usual. The buffer has TI's layout. A request is the data length (2 bytes), the command, eight parameter bytes and the data; a response is the data length, eight parameter bytes (the result first) and the data.
- **Syscalls at 0x0180 + N.** `exit`, `open`, `close`, `read`, `lseek`, `kill` and `fstat` are symbols at 0x0180 plus their libgloss syscall number. When PC reaches 0x0180–0x01BF, mspsim performs the call with the arguments in R12–R14, puts the result in R12 and returns as `ret` would. A real chip resets on a fetch from there, so no working program executes there otherwise. A `call` and a tail-call `br` both work.

Only the console is served. Writes to fds 1 and 2 go to stdout and stderr, and reads from fd 0 come from stdin. On a terminal, reads are in line mode with echo. A byte the UART has already taken from stdin comes first. `exit()` ends the run with its status, and `kill()` with 128 plus the signal. `close` of fds 0–2 succeeds. Opening, seeking, `fstat`, unlinking and the other CIO commands fail with -1, so newlib treats the console as a terminal it cannot `fstat`.

The trace (`-t`) shows each call, as in `*** CIO write(1, 36) = 36` or `*** syscall read(0x0000, 0xfee2, 0x0400) = 11`.

## Tracing

`-t` prints each executed instruction to stderr (`-o FILE` writes it to a file). Each instruction is followed by its data loads and stores in order, then the new values of the registers it changed:

```
c00a: 5292 0200 0202   add   &0x0200, &0x0202
      Read   [0200] = 0005
      Read   [0202] = 0003
      Write  [0202] = 0008
c010: 4fe5 0003        mov.b @r15, 3(r5)
      Readb  [1234] = 41
      Writeb [0207] = 41
c014: 8314             sub   #0x0001, r4
      SR = 0004 [N]
      R4 = ffff
c016: 12b0 c100        call  #0xc100 <putc>
      Write  [03fc] = c01a
      SP = 03fc
```

Instruction fetches and PC changes are left out, and SR changes also show the flags that are set. An interrupt entry appears as `*** interrupt vector 0xffee`, followed by the stack pushes, the vector load and the PC, SP and SR changes. Jump and call targets and absolute addresses are annotated with ELF symbols, as in `<putc>` or `<main+0x1a>`.

## Debugger

`-g` starts the program paused at the debugger prompt; Ctrl-] during a run on a terminal opens it too. The prompt has line editing and history, an empty line repeats the last command, and Ctrl-C stops a run.

| Command | What it does |
|---|---|
| `step [N]`, `s` | Execute N instructions (default 1) |
| `run`, `r`, `c` | Run until a breakpoint, the stop register, or another stop condition |
| `break ADDR`, `b` | Set a breakpoint |
| `bps` | List the breakpoints |
| `dis [N] [ADDR]` | Disassemble N instructions (default 10) from ADDR (default PC) |
| `dump [ADDR\|Rn]` | Show 32 bytes of memory at ADDR, or at the address in register Rn |
| `set ADDR\|Rn VALUE` | Set a memory word or a register; VALUE is hex |
| `regs` | Show the registers and the next instruction |
| `trace on\|off` | Switch tracing on or off |
| `reset` | Reset the CPU and the UART; memory is kept |
| `quit`, `q` | Exit |
| `help`, `h` | List the commands |

ADDR is an ELF symbol name or a hex address, so `break main` works.

## Tests

`make test` runs these suites:
- **Unit tests:** `test/test_*.c`.
- **openMSP430 instruction tests:** the tests of the [openMSP430](https://opencores.org/projects/openmsp430) project in `test/openmsp430/`, assembled by a small test assembler (`test/asm.c`).
- **Command-line tests:** `test/cli_test.sh`.
- **Firmware tests:** when `msp430-elf-gcc` is installed, the C programs in `test/firmware/` are compiled and run as well, with `MSP430_FLAGS` (default `-mcpu=msp430 -msim -Os`). A `NAME.input` file, if present, is the program's stdin.

## Source layout

| Path | Contents |
|---|---|
| `src/main.c` | Command-line front-end: options, run loop, debugger prompt |
| `src/emulator.c` | Emulator object, reset and run loop |
| `src/cpu/` | Decoder, instruction formats, flags, interrupts, cycle counts |
| `src/mem/` | Memory bus, device dispatch, stop register |
| `src/uart/` | Console UART |
| `src/hostio/` | newlib's host I/O: the CIO breakpoint and the syscalls at 0x0180 |
| `src/loader/` | ELF, Intel HEX and raw loaders; symbol table |
| `src/debug/` | Debugger commands, disassembler, register display, trace |
| `src/linenoise/` | Line editor (vendored) |
| `test/` | Test harness, test assembler and tests |

## License

mspsim is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. See `COPYING` for the full text.

The emulator core comes from [MSP430-Emulator](https://github.com/RudolfGeosits/MSP430-Emulator) by Rudolf Geosits (GPL-3.0), via [msp430emu](https://github.com/zceemja/msp430emu).

The line editor is [linenoise](https://github.com/antirez/linenoise) by Salvatore Sanfilippo and Pieter Noordhuis, under the BSD-2-Clause license; see `src/linenoise/LICENSE`.

The instruction tests in `test/openmsp430/` are from openMSP430 by Olivier Girard, under the LGPL-2.1 or later. `test/binutils/add.s` is from the GNU binutils-gdb simulator test suite (GPL-3.0 or later).
