# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

A command-line simulator of the classic MSP430 CPU, written in C. The core is forked from RudolfGeosits/MSP430-Emulator, by way of a Python/wxPython wrapper that has since been removed. The project is being converted step by step according to `Plan.md`. Read it before making structural changes: it records the decisions made (classic MSP430 only, CPU plus a console UART, CMake, a bundled line editor, POSIX only, a trace flag) and the order of the steps.

## Build and run

```bash
make                                    # or: cmake -B build && cmake --build build
./build/mspsim [-g] [-q] [-n N] [-b ADDR] fw.bin   # front-end: src/main.c
make test                               # all tests (ctest), including test/cli_test.sh
./build/test/test_interrupts [case_name] # one test file, or a single case
```

- The build uses `-Wall -Wextra` and is warning-free in both Debug and RelWithDebInfo (the Makefile default, which adds `-O2` checks); keep it that way. `make format` runs clang-format.
- **Headers are self-contained, and include order never matters**, so clang-format can sort includes freely.
  - The `header_check` target compiles every header on its own, included twice.
  - Include by path from `emulator/` (e.g. `"cpu/registers.h"`, never `"../"`).
  - Every file includes what it uses. There is no umbrella header: `emulator.h` declares the `Emulator`/`Cpu`/`Debugger` types, `StopReason`, and `emu_create()`/`emu_destroy()`/`emu_reset()`/`emu_run()`.
- **Unit tests** live in `test/`, one executable per `test_*.c` file, each registered with ctest in `test/CMakeLists.txt` (add new files to its `foreach` list).
  - `harness.h` provides `TEST()`, `CHECK`/`CHECK_EQ`, `emu_new()`, `PROGRAM(addr, words...)` for hand-encoded instructions, `poke`/`peek` and `step`.
  - `emu_new()` wraps `emu_create()`; `poke`/`peek` go through the memory bus.
  - It also defines the front-end hooks: `print_console` captures into `console_text`, `uart_tx` into `uart_output`, and `uart_rx` reads from `uart_input` (set it before stepping).
  - Each file's `main()` lists its cases with `T(name)`.
  - `test/cli_test.sh` runs the `mspsim` binary on hand-assembled images and checks stdout and exit statuses (ctest `cli`).
- `third_party/linenoise/` is vendored unmodified (BSD-2-Clause); `make format` skips it.

## Firmware format

`load_binary()` (`emulator/utilities.c`) loads a **raw binary** image at the `-b` address (default 0xC000); it prints nothing and returns -1 with `errno` set on failure. ELF and Intel HEX are rejected until Plan Step 8. `cpu_reset()` hard-codes PC = 0xC000.

## Architecture

**Front-end hooks:** `emulator/io.h` declares functions the front-end (`src/main.c`) implements: `print_console` for diagnostic and debugger text, and `uart_tx`/`uart_rx` for the console UART (`uart_rx` returns -1 when no byte is ready and `UART_EOF` at end of input). The core never calls `printf`; it prints with `emu_printf()` (`io.c`), which goes through `print_console`.

**Front-end:** `src/main.c` parses options with `getopt_long`, then runs in batch mode, or in the interactive debugger with `-g` (a linenoise prompt; an empty line repeats the last command). UART output goes to stdout. Other text goes to stderr in batch mode (`-q` drops it) and to stdout at the debugger prompt. During a run, a tty stdin is raw; Ctrl-] or SIGINT set `emu->break_request`. Ctrl-] enters the debugger; in batch mode SIGINT exits with 130. Exit statuses: the stop-register value, 124 cycle limit, 125 asleep with no wake-up, 130 interrupted, 132 illegal instruction, 1 load error, 2 usage.

**Run loop:** `emu_run(emu, max_cycles)` (`emulator.c`) steps until `emu->stop` is set: by the stop register, an illegal instruction (PC is left on it), a breakpoint (checked after each step, so resuming from one works), a break request, the cycle limit, or `CPUOFF` with no possible wake-up (GIE clear, or no RX interrupt enabled, or stdin at EOF). While asleep with a possible wake-up it skips ahead to the next input poll.

**Memory:** the 64 KB address space is `emu->mem`, all plain RAM. The CPU touches it only through the bus in `memory/memory.h`: `mem_read(emu, addr, size, kind)` and `mem_write(emu, addr, val, size)`, where size is 1 or 2 bytes, word accesses ignore address bit 0, and `kind` is `ACC_FETCH` (opcode and extension words, via `fetch()`) or `ACC_DATA`. The debugger and the loader may use `emu->mem` directly.

**Devices:** data accesses below 0x0200 go through `io_read`/`io_write` in `memory.c`, which dispatch to the device handlers. The only device is the console UART (`uart/uart.c`), a USCI_A0 subset: `IE2` 0x0001 (RXIE), `IFG2` 0x0003 (TXIFG always 1, RXIFG = byte waiting), `UCA0RXBUF` 0x0066 and `UCA0TXBUF` 0x0067; its RX interrupt is vector 7 (0xFFEE). Device state is mirrored in `emu->mem`, so `dump` shows it. Input is polled every `UART_POLL_CYCLES` from `cpu_step()` and on each `IFG2` read. A write to 0x01FE sets `emu->stop = EMU_PROGRAM` and `emu->exit_code`. `emu_reset()` resets the CPU, the UART and the stop state.

**Registers:** `cpu->r[16]` is aliased by `pc`, `sp`, `sr`, `cg2`, `r4`…`r15`. SR is a plain `uint16_t` with `SR_C`, `SR_Z`, `SR_N`, `SR_GIE`, `SR_CPUOFF`, … masks; use `set_flag()`. Register-mode writes go through `reg_write()`: byte writes clear the high byte, and R3 discards writes.

**Execution:** `cpu_step()` (`cpu/registers.c`) decodes and executes one instruction (skipped when `CPUOFF` is set), then calls `handle_interrupts()` (which returns the vector number taken, or -1), and advances `cpu->cycles`. For now it counts a flat 4 cycles per instruction, 1 per idle step and 6 per interrupt entry.

**Interrupts:** `cpu->irq_pending` is a mask with bit N requesting the vector at 0xFFE0 + 2N; a higher N has higher priority. Sources assert and deassert requests with `cpu_set_irq()`. They're level-sensitive: the source must deassert. The exception is the NMI (vector 14), which ignores GIE and is cleared when accepted.

**Instruction decoding:** `decoder.c` dispatches to `formatI.c` (two-operand), `formatII.c` (single-operand) and `formatIII.c` (jumps). Flags are computed from values in `flag_handler.c`.
- Operands: `decode_operand()` (`decoder.c`) resolves an addressing mode into an `Operand` (`OPND_REG`, `OPND_MEM` with an address, or `OPND_CONST` for immediates and the constant generators), fetching its extension word and applying `@Rn+`. Instructions then use `operand_read()`/`operand_write()`, so a read-modify-write does one load and one store. Format I reads the source before decoding the destination, as hardware does.
- `decode(emu, insn, l)` executes when `l` is NULL. Otherwise it disassembles into the `Listing` `l` (words, mnemonic, operand text), with no data accesses and no autoincrement; the execute path builds no text. `disassemble_at()` (`debugger/disassembler.c`) fills a `Listing` for an address, and `format_listing()` renders the one line format used by both `dis` and the trace: `c004: 40b2 5a80 0120   mov   #0x5a80, &0x0120`. Use `str_append()` (`utilities.h`) for text, not `strncat`.

**Tracing:** `emu->trace` (`-t`, `-o FILE`, or `trace on`) makes `cpu_step()` call `trace_begin()`/`trace_end()` (`debugger/trace.c`) around each instruction, and `trace_interrupt()` after interrupt entry. While tracing, the bus logs every `ACC_DATA` access through `trace_access()`; fetches are not logged. The output is the listing line, then `R`/`W`/`Rb`/`Wb` accesses in order, then changed registers (PC only for interrupts; SR decoded). It goes to `emu->trace_file`, or stderr. Golden tests are in `test/test_trace.c`.

**Debugger:** `exec_cmd()` in `debugger/debugger.c` runs one command (step, run/c, dis, dump, set, break, bps, regs, trace, reset, quit, help) and returns `DBG_STAY`, `DBG_RUN` or `DBG_QUIT`; the front-end does the running. `report_stop()` prints why the CPU stopped. The core keeps no global state.
