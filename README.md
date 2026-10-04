# MSP430 Simulator

A command-line simulator of the classic MSP430 CPU. Work in progress: see [Plan.md](Plan.md).

The emulator core comes from
[https://github.com/RudolfGeosits/MSP430-Emulator](https://github.com/RudolfGeosits/MSP430-Emulator)
(GPL-3.0), via [https://github.com/zceemja/msp430emu](https://github.com/zceemja/msp430emu).

## Build

You need CMake 3.16 or newer and a C11 compiler (gcc or clang). The top-level Makefile wraps CMake:

| Command | What it does |
|---|---|
| `make` | Configure `build/` (RelWithDebInfo) on first use, then build everything |
| `make test` | Build, then run the unit tests with ctest |
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

```bash
./build/mspsim firmware.bin [max_steps]
```

The firmware is a raw binary image loaded at 0xC000.

The console UART uses the USCI_A0 registers of the MSP430G2xx: a byte written to `UCA0TXBUF` (0x0067) goes to stdout, and stdin is read through `UCA0RXBUF` (0x0066), with `IFG2` (0x0003) and `IE2` (0x0001) flags as on the real chip. On a terminal, Ctrl-] stops the run. A write to 0x01FE stops the simulator, and the value written becomes the exit status.
