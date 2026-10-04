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
| `make install` | Install `msp430-sim` into `~/.local/bin`, or into `/usr/local/bin` if `~/.local` doesn't exist |
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
./build/msp430-sim firmware.bin [max_steps]
```

The firmware is a raw binary image loaded at 0xC000.
