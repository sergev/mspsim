# Plan: msp430emu → generic MSP430 simulator with CLI

## Goal

A standalone C program, `mspsim`, built with CMake, that simulates a classic MSP430 CPU (16-bit, 64 KB address space, no MSP430X/CPUX). It has:

- a flat memory model;
- a simple console UART;
- batch and interactive (debugger) modes;
- a tracing flag that prints each executed instruction, the registers it changed, and its data memory loads and stores.

Python, wxPython, the GUI, the websocket server, the LaunchPad-specific peripherals and Windows/MSVC support are all removed.

## Decisions

| Topic | Decision |
|---|---|
| CPU | Classic MSP430 only; no MSP430X |
| Peripherals | CPU + simple console UART only; BCM, Timer_A, Port 1 and full USCI are removed |
| Build | CMake |
| Line editing | Small bundled line editor (vendored linenoise, BSD-2-Clause, GPL-compatible) |
| Platforms | Linux/macOS (POSIX); no MSVC |
| GDB stub | No |
| Tracing | `--trace`: executed instructions, changed registers, data loads/stores; no instruction fetches |
| Console UART | G2xx USCI_A0-compatible register addresses |
| Stop | A write to 0x01FE stops the simulator; the value written is the exit code |
| Memory | All 64 KB is plain RAM; no flash write protection |

## Current state

Steps 1–9 are done; the remaining steps keep their original numbers.

- **Build:** CMake builds the `msp430core` library, vendored linenoise, the `mspsim` front-end (`src/main.c`) and the tests (`test/`, run with `ctest`). It is warning-free under `-Wall -Wextra`. Headers are self-contained; the `header_check` target enforces it.
- **Removed:** the Python/wxPython GUI, the websocket server, MSVC support, and all peripherals (clock module, Timer_A, Port 1, USCI).
- **CPU core:** all 27 instructions, flags and addressing modes are tested: the 32 openMSP430 instruction tests pass, plus flag, emulated-instruction and cycle tables in `test/test_cpu.c`. `cpu->cycles` follows the SLAU144 cycle tables; the total is reported on exit.
- **Tests:** a test assembler (`test/asm.c`) lets tests be written in assembly. Firmware tests in C (`test/firmware/`) run when `msp430-elf-gcc` is available.
- **Interrupts:** `cpu->irq_pending` is a 16-bit mask of level-sensitive requests, raised with `cpu_set_irq()`. The NMI is edge-triggered. Interrupt entry clears SR except SCG0.
- **Memory:** `emu->mem`, reached by the CPU only through `mem_read()`/`mem_write()` (`memory/memory.h`), with `ACC_FETCH` for opcode and extension words and `ACC_DATA` for everything else. Instructions use operand descriptors (`decode_operand()`, `operand_read()`, `operand_write()`), so each read-modify-write is one load and one store. Accesses below 0x0200 are dispatched to the device handlers.
- **Console UART:** `uart/uart.c`, the USCI_A0 subset below, mirrored in `emu->mem`. Input and output go through the front-end hooks `uart_tx`/`uart_rx` (`io.h`). The front-end uses stdout and non-blocking stdin, raw on a tty during a run. Input is polled every 4096 cycles and on each `IFG2` read. Other USCI registers are plain memory.

| Address | Register | Behaviour |
|---|---|---|
| 0x0067 | `UCA0TXBUF` | Write → byte is sent to stdout immediately |
| 0x0066 | `UCA0RXBUF` | Read → next byte from stdin; clears RXIFG |
| 0x0003 | `IFG2` | bit 1 `UCA0TXIFG` always 1; bit 0 `UCA0RXIFG` = input byte available |
| 0x0001 | `IE2` | bit 0 `UCA0RXIE` enables the RX interrupt (vector 0xFFEE) |

- **Stop register:** a write to 0x01FE sets `emu->stop = EMU_PROGRAM` and `emu->exit_code`.
- **Registers:** `cpu->r[16]` with named aliases; SR is a plain `uint16_t` with `SR_*` masks, so its reserved bits survive.
- **Core state:** the core has no globals. `emu_create()`/`emu_destroy()`/`emu_reset()` manage an emulator; `emu_run()` runs it until a `StopReason`.
- **Front-end:** `src/main.c`, with `getopt_long` options, batch mode and a linenoise debugger prompt. The exit status reflects the stop reason. Core output goes through `emu_printf()` and the `print_console()` hook: stderr in batch mode, stdout at the prompt.
- **Debugger:** `exec_cmd()` in `debugger.c` runs one command and returns stay/run/quit. `dis` and the trace share one listing format.
- **Tracing:** `-t`, `-o FILE` or `trace on` enable `debugger/trace.c`: each instruction, its data loads/stores and its register changes, plus interrupt entries. When tracing is off the bus pays one branch per access. Golden tests are in `test/test_trace.c`.
- **Loading:** `loader/loader.c` loads ELF32 (`PT_LOAD` at physical addresses, plus the symbol table) and Intel HEX, detected from the contents; raw binaries need `-b ADDR`. PC comes from the reset vector, or from the loader's entry address when the vector is erased. The debugger accepts symbol names as addresses, and `dis` and the trace annotate targets with symbols.

## Remaining steps

### Step 10 — Source layout and docs

1. Flatten the tree into `src/` (`cpu/`, `mem/`, `uart/`, `debug/`, `main.c`), plus `test/` and `third_party/`.
2. Rewrite `README.md`: the build, the usage, the console UART register map, the trace format and the debugger commands. Keep the GPL-3.0 notice, the upstream credit and the linenoise license.
3. Update `CLAUDE.md`.

## Commit order

10 is the last step.

Each step is one or more commits that leave the tree building.

