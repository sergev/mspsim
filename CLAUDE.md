# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

A command-line simulator of the classic MSP430 CPU, written in C. The core is forked from RudolfGeosits/MSP430-Emulator, by way of a Python/wxPython wrapper that has since been removed. The project is being converted step by step according to `Plan.md`. Read it before making structural changes: it records the decisions made (classic MSP430 only, CPU plus a console UART, CMake, a bundled line editor, POSIX only, a trace flag) and the order of the steps.

## Build and run

```bash
make                                    # or: cmake -B build && cmake --build build
./build/mspsim fw.bin [max_steps]   # temporary driver (emulator/cli/main.c): runs, then dumps registers
make test                               # all unit tests (ctest)
./build/test/test_interrupts [case_name] # one test file, or a single case
```

- The build uses `-Wall -Wextra` and is warning-free in both Debug and RelWithDebInfo (the Makefile default, which adds `-O2` checks); keep it that way. `make format` runs clang-format.
- **Headers are self-contained, and include order never matters**, so clang-format can sort includes freely.
  - The `header_check` target compiles every header on its own, included twice.
  - Include by path from `emulator/` (e.g. `"cpu/registers.h"`, never `"../"`).
  - Every file includes what it uses. There is no umbrella header: `emulator.h` declares the `Emulator`/`Cpu`/`Debugger` types plus `emu_create()`/`emu_destroy()`.
- **Unit tests** live in `test/`, one executable per `test_*.c` file, each registered with ctest in `test/CMakeLists.txt` (add new files to its `foreach` list).
  - `harness.h` provides `TEST()`, `CHECK`/`CHECK_EQ`, `emu_new()`, `PROGRAM(addr, words...)` for hand-encoded instructions, `poke`/`peek` and `step`.
  - `emu_new()` wraps `emu_create()`; `poke`/`peek` go through the memory bus.
  - It also defines the front-end hooks: `print_console` captures into `console_text`, `uart_tx` into `uart_output`, and `uart_rx` reads from `uart_input` (set it before stepping).
  - Each file's `main()` lists its cases with `T(name)`.

## Firmware format

`load_firmware()` (`emulator/utilities.c`) reads a **raw binary** image and loads it at 0xC000, up to 16 KB. `cpu_reset()` hard-codes PC = 0xC000.

## Architecture

**Front-end hooks:** `emulator/io.h` declares functions the front-end implements: `print_console` for diagnostic text (for now in `emulator/cli/stub_io.c`), and `uart_tx`/`uart_rx` for the console UART (in `emulator/cli/main.c`: stdout, and non-blocking stdin in raw mode on a tty, where Ctrl-] stops the run). Many call sites still `printf` *and* `print_console` the same text, so output appears twice. Plan Step 6 fixes that.

**Memory:** the 64 KB address space is `emu->mem`, all plain RAM. The CPU touches it only through the bus in `memory/memory.h`: `mem_read(emu, addr, size, kind)` and `mem_write(emu, addr, val, size)`, where size is 1 or 2 bytes, word accesses ignore address bit 0, and `kind` is `ACC_FETCH` (opcode and extension words, via `fetch()`) or `ACC_DATA`. The debugger and the loader may use `emu->mem` directly.

**Devices:** data accesses below 0x0200 go through `io_read`/`io_write` in `memory.c`, which dispatch to the device handlers. The only device is the console UART (`uart/uart.c`), a USCI_A0 subset: `IE2` 0x0001 (RXIE), `IFG2` 0x0003 (TXIFG always 1, RXIFG = byte waiting), `UCA0RXBUF` 0x0066 and `UCA0TXBUF` 0x0067; its RX interrupt is vector 7 (0xFFEE). Device state is mirrored in `emu->mem`, so `dump` shows it. Input is polled every `UART_POLL_CYCLES` from `cpu_step()` and on each `IFG2` read. A write to 0x01FE sets `emu->stopped` and `emu->exit_code` and clears `cpu->running`. `emu_reset()` resets the CPU, the UART and the stop state.

**Registers:** `cpu->r[16]` is aliased by `pc`, `sp`, `sr`, `cg2`, `r4`…`r15`. SR is a plain `uint16_t` with `SR_C`, `SR_Z`, `SR_N`, `SR_GIE`, `SR_CPUOFF`, … masks; use `set_flag()`. Register-mode writes go through `reg_write()`: byte writes clear the high byte, and R3 discards writes.

**Execution:** `cpu_step()` (`cpu/registers.c`) decodes and executes one instruction (skipped when `CPUOFF` is set), then calls `handle_interrupts()`, and advances `cpu->cycles`. For now it counts a flat 4 cycles per instruction, 1 per idle step and 6 per interrupt entry.

**Interrupts:** `cpu->irq_pending` is a mask with bit N requesting the vector at 0xFFE0 + 2N; a higher N has higher priority. Sources assert and deassert requests with `cpu_set_irq()`. They're level-sensitive: the source must deassert. The exception is the NMI (vector 14), which ignores GIE and is cleared when accepted.

**Instruction decoding:** `decoder.c` dispatches to `formatI.c` (two-operand), `formatII.c` (single-operand) and `formatIII.c` (jumps). Flags are computed from values in `flag_handler.c`.
- Operands: `decode_operand()` (`decoder.c`) resolves an addressing mode into an `Operand` (`OPND_REG`, `OPND_MEM` with an address, or `OPND_CONST` for immediates and the constant generators), fetching its extension word and applying `@Rn+`. Instructions then use `operand_read()`/`operand_write()`, so a read-modify-write does one load and one store. Format I reads the source before decoding the destination, as hardware does.
- The disassembler runs the same decode path with `DISASSEMBLE` instead of `EXECUTE`: no data accesses, no autoincrement. The text is collected in a `Listing` and printed by `print_listing()`. Use `str_append()` (`utilities.h`) for text, not `strncat`.

**Debugger:** `exec_cmd()` in `debugger/debugger.c` parses and runs the debugger commands (step, run, dis, dump, set, break, bps, regs, reset, quit, help). The SIGINT handler lives in the front-end (`cli/main.c`); the core keeps no global state.
