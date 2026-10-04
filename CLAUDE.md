# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

A command-line simulator of the classic MSP430 CPU, written in C. The core is forked from RudolfGeosits/MSP430-Emulator, by way of a Python/wxPython wrapper that has since been removed. The project is being converted step by step according to `Plan.md`. Read it before making structural changes: it records the decisions made (classic MSP430 only, CPU plus a console UART, CMake, a bundled line editor, POSIX only, a trace flag) and the order of the steps.

## Build and run

```bash
make                                    # or: cmake -B build && cmake --build build
./build/msp430-sim fw.bin [max_steps]   # temporary driver (emulator/cli/main.c): runs, then dumps registers
make test                               # all unit tests (ctest)
./build/test/test_interrupts [case_name] # one test file, or a single case
```

- The build uses `-Wall -Wextra` and is warning-free in both Debug and RelWithDebInfo (the Makefile default, which adds `-O2` checks); keep it that way. `make format` runs clang-format.
- **Headers are self-contained, and include order never matters**, so clang-format can sort includes freely.
  - The `header_check` target compiles every header on its own, included twice.
  - Include by path from `emulator/` (e.g. `"cpu/registers.h"`, never `"../"`).
  - Every file includes what it uses. There is no umbrella header: `emulator.h` only declares the `Emulator`/`Cpu`/`Debugger` types.
- **Unit tests** live in `test/`, one executable per `test_*.c` file, each registered with ctest in `test/CMakeLists.txt` (add new files to its `foreach` list).
  - `harness.h` provides `TEST()`, `CHECK`/`CHECK_EQ`, `emu_new()`, `PROGRAM(addr, words...)` for hand-encoded instructions, `poke`/`peek` and `step`.
  - It also defines `print_console`, which captures output into `console_text`.
  - Each file's `main()` lists its cases with `T(name)`.

## Firmware format

`load_firmware()` (`emulator/utilities.c`) reads a **raw binary** image and loads it at 0xC000, up to 16 KB. `cpu_reset()` hard-codes PC = 0xC000.

## Architecture

**Front-end hook:** the core reports text through `print_console`, which is declared in `emulator/io.h` and implemented by the front-end (for now `emulator/cli/stub_io.c`). Many call sites still `printf` *and* `print_console` the same text, so output appears twice. Plan Step 6 fixes that.

**Memory:** `initialize_msp_memspace()` allocates one global 64 KB `MEMSPACE` array. The CPU reads and writes it directly through `get_addr_ptr()` host pointers. Plan Step 4 replaces this with a memory bus. There are no peripherals; the console UART comes back in Plan Step 5.

**Execution:** `cpu_step()` (`cpu/registers.c`) decodes and executes one instruction (skipped when `CPUOFF` is set), then calls `handle_interrupts()`, and advances `cpu->cycles`. For now it counts a flat 4 cycles per instruction, 1 per idle step and 6 per interrupt entry.

**Interrupts:** `cpu->irq_pending` is a mask with bit N requesting the vector at 0xFFE0 + 2N; a higher N has higher priority. Sources assert and deassert requests with `cpu_set_irq()`. They're level-sensitive: the source must deassert. The exception is the NMI (vector 14), which ignores GIE and is cleared when accepted.

**Instruction decoding:** `decoder.c` dispatches to `formatI.c` (two-operand), `formatII.c` (single-operand) and `formatIII.c` (jumps). Flags are computed in `flag_handler.c`. The disassembler runs the same decode path with `DISASSEMBLE` instead of `EXECUTE`, so the disassembly text is built inside the instruction implementations. Use `str_append()` (`utilities.h`) for that text, not `strncat`.

**Debugger:** `exec_cmd()` in `debugger/debugger.c` parses and runs the debugger commands (step, run, dis, dump, set, break, bps, regs, reset, quit, help).
