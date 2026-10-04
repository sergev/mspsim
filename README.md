# MSP430 Simulator

A command-line simulator of the classic MSP430 CPU. Work in progress: see [Plan.md](Plan.md).

The emulator core comes from
[https://github.com/RudolfGeosits/MSP430-Emulator](https://github.com/RudolfGeosits/MSP430-Emulator)
(GPL-3.0), via [https://github.com/zceemja/msp430emu](https://github.com/zceemja/msp430emu).

The line editor is [linenoise](https://github.com/antirez/linenoise) (BSD-2-Clause, see `third_party/linenoise/LICENSE`).

## Build

You need CMake 3.16 or newer and a C11 compiler (gcc or clang). The top-level Makefile wraps CMake:

| Command | What it does |
|---|---|
| `make` | Configure `build/` (RelWithDebInfo) on first use, then build everything |
| `make test` | Build, then run the unit and command-line tests with ctest |
| `make install` | Install `mspsim` into `~/.local/bin`, or into `/usr/local/bin` if `~/.local` doesn't exist |
| `make debug` | Reconfigure `build/` for a Debug build; follow with `make` |
| `make format` | Reformat all C sources with clang-format (uses `.clang-format`) |
| `make clean` | Remove `build/` |

Or use CMake directly:

```bash
cmake -B build
cmake --build build
ctest --test-dir build
```

## Run

```
mspsim [options] firmware
  -b, --binary ADDR     raw binary loaded at ADDR (default 0xC000)
  -g, --debug           start paused in the interactive debugger
  -n, --max-cycles N    stop after N cycles
  -t, --trace           trace executed instructions, register changes, loads/stores
  -o, --trace-file F    write trace to F instead of stderr (implies -t)
  -q, --quiet           no banner/diagnostics, only UART output
```

The firmware is a raw binary image; execution starts at 0xC000. ELF and Intel HEX are not supported yet.

UART output goes to stdout and diagnostics to stderr, so `mspsim -q fw.bin > out.txt` captures just what the program prints.

The run ends when the program writes to the stop register, hits an illegal instruction, reaches the `-n` cycle limit, or goes to sleep (`CPUOFF`) with no way to wake up. The exit status says which:

| Status | Meaning |
|---|---|
| value written to 0x01FE | the program stopped itself |
| 124 | cycle limit reached |
| 125 | CPU off with no wake-up source |
| 130 | interrupted with Ctrl-C |
| 132 | illegal instruction |
| 1 | the firmware could not be loaded |
| 2 | bad command line |

### Tracing

`-t` prints each executed instruction to stderr (`-o FILE` writes it to a file), followed by its data loads and stores and the registers it changed:

```
c00a: 5292 0200 0202   add   &0x0200, &0x0202
      R  [0200] -> 0005
      R  [0202] -> 0003
      W  [0202] <- 0008
c010: 4fe5 0003        mov.b @r15, 3(r5)
      Rb [1234] -> 41
      Wb [0207] <- 41
c000: 8314             sub   #0x0001, r4
      SR 0000 -> 0004 [N]
      R4 0000 -> ffff
```

Instruction fetches and PC changes are left out. Interrupt entry shows as `*** interrupt vector 0xffee`, followed by the stack pushes, the vector load and the PC, SP and SR changes. In the debugger, `trace on` and `trace off` switch tracing during a session.

### Debugger

`-g`, or Ctrl-] during a run on a terminal, opens the debugger prompt, with line editing and history. Type `help` for the commands: `step [N]`, `run`, `dis [N] [ADDR]`, `dump ADDR|Rn`, `set ADDR|Rn VALUE`, `break ADDR`, `bps`, `regs`, `trace on|off`, `reset` and `quit`. An empty line repeats the last command, and Ctrl-C stops a run.

The console UART uses the USCI_A0 registers of the MSP430G2xx: a byte written to `UCA0TXBUF` (0x0067) goes to stdout, and stdin is read through `UCA0RXBUF` (0x0066), with `IFG2` (0x0003) and `IE2` (0x0001) flags as on the real chip. On a terminal, stdin is raw while the program runs, so keys go straight to the UART.
