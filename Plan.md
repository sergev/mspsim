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

Steps 1–4 are done; the remaining steps keep their original numbers.

- **Build:** CMake builds the `msp430core` library, a temporary `mspsim` driver (`emulator/cli/main.c`) and the unit tests (`test/`, run with `ctest`). It is warning-free under `-Wall -Wextra`. Headers are self-contained; the `header_check` target enforces it.
- **Removed:** the Python/wxPython GUI, the websocket server, MSVC support, and all peripherals (clock module, Timer_A, Port 1, USCI).
- **CPU core:** the decoder, Format I/II/III instructions and flags work. `cpu->cycles` counts a flat 4 cycles per instruction.
- **Interrupts:** `cpu->irq_pending` is a 16-bit mask of level-sensitive requests, raised with `cpu_set_irq()`. The NMI is edge-triggered. Interrupt entry clears SR except SCG0.
- **Memory:** `emu->mem`, reached by the CPU only through `mem_read()`/`mem_write()` (`memory/memory.h`), with `ACC_FETCH` for opcode and extension words and `ACC_DATA` for everything else. Instructions use operand descriptors (`decode_operand()`, `operand_read()`, `operand_write()`), so each read-modify-write is one load and one store. The bus has no device dispatch yet; Step 5 adds it.
- **Registers:** `cpu->r[16]` with named aliases; SR is a plain `uint16_t` with `SR_*` masks, so its reserved bits survive.
- **Core state:** the core has no globals. `emu_create()`/`emu_destroy()` build and free an emulator; the SIGINT handler is in the front-end.
- **Front-end hook:** about 30 call sites use `print_console()` (`emulator/io.h`). Many of them also `printf` the same text, so output appears twice.
- **Debugger:** `exec_cmd()` in `debugger.c` parses commands.
- **Loading:** only raw `.bin` images loaded at 0xC000. `cpu_reset()` hard-codes PC = 0xC000.
- **Known ALU bugs, left for Step 9:** `SUB` never clears C; `ADDC`/`SUBC` compute C without the carry-in; `DADD` is a no-op; V on subtraction is suspect.

## Remaining steps

### Step 5 — Console UART

A minimal memory-mapped device. Its registers are a subset of USCI_A0 at the MSP430G2xx addresses, so ordinary `msp430-elf-gcc` programs that print over UART run unmodified:

| Address | Register | Behaviour |
|---|---|---|
| 0x0067 | `UCA0TXBUF` | Write → byte is sent to stdout immediately |
| 0x0066 | `UCA0RXBUF` | Read → next byte from stdin; clears RXIFG |
| 0x0003 | `IFG2` | bit 1 `UCA0TXIFG` always 1; bit 0 `UCA0RXIFG` = input byte available |
| 0x0001 | `IE2` | bit 0 `UCA0RXIE` enables the RX interrupt (vector 0xFFEE) |

- Other USCI registers (`UCA0CTL*`, `UCA0BR*`, …) are plain read/write memory. Baud-rate setup works but has no effect.
- RX takes input from stdin, non-blocking, through the front-end's `uart_rx` callback. The UART is not tied to stdio itself. When stdin is a tty in run mode, it is put in raw mode, and Ctrl-] returns to the debugger.
- **Bus dispatch:** `mem_read()`/`mem_write()` dispatch the UART addresses and the stop register to their handlers; everything else stays plain RAM.
- **Simulator stop:** a byte or word write to `0x01FE` stops the simulator. The value written becomes the exit code, so test programs can terminate cleanly.

### Step 6 — CLI front-end and line editor

1. Vendor linenoise into `third_party/linenoise/`, with its license file, and build it as part of the CMake project.
2. Write `src/main.c`, with argument parsing via `getopt_long`:
   ```
   mspsim [options] firmware
     -b, --binary ADDR     raw binary loaded at ADDR (default: auto-detect ELF / Intel HEX)
     -g, --debug           start paused in the interactive debugger
     -n, --max-cycles N    stop after N cycles
     -t, --trace           trace executed instructions, register changes, loads/stores
     -o, --trace-file F    write trace to F instead of stderr
     -q, --quiet           no banner/diagnostics, only UART output
   ```
3. **Run loop:** `emu_run(emu, max_cycles)` runs at full speed. Without a clock module, there is no wall-clock throttling.
4. **Exit conditions:**
   - quit;
   - `--max-cycles` reached;
   - the stop register is written;
   - an illegal instruction;
   - `CPUOFF` with `GIE` clear, or with GIE set but no possible wake-up source (stdin at EOF).

   The exit status reflects which one occurred.
5. **Interactive debugger:** keep the `exec_cmd` commands (`step`, `run`, `dis`, `dump`, `set`, `break`, `bps`, `regs`, `reset`, `quit`, `help`). Use linenoise for the prompt, with history. Add `trace on|off` to toggle tracing at runtime. SIGINT during `run` returns to the prompt.
6. Route diagnostic and debugger output through one `emu_printf()`, replacing the `sprintf`+`printf`+`print_console` triples. Make `register_display.c` coloured only when stdout is a tty.
7. **Check:** a hand-assembled image that writes "Hello\n" to `UCA0TXBUF` prints it, then exits via the stop register.

### Step 7 — Tracing

**Mechanism:**
- Before each instruction, snapshot R0–R15.
- The memory bus appends every `ACC_DATA` access to a per-instruction log; `ACC_FETCH` is not logged.
- After the instruction, print one line for the instruction, followed by the loads/stores in execution order and the registers whose values changed. The PC is excluded, because it is implied by the next line; SR is shown decoded.

**Example output format:**
```
c004: 40b2 5a80 0120   mov   #0x5a80, &0x0120
      W  [0120] <- 5a80
c00a: 4031 0400        mov   #0x0400, sp
      SP 0000 -> 0400
c00e: 5592 0200 0202   add   &0x0200, &0x0202
      R  [0200] -> 0005
      R  [0202] -> 0003
      W  [0202] <- 0008
      SR 0000 -> 0000
c014: 4fe5 0003        mov.b @r15, 3(r5)
      Rb [1234] -> 41
      Wb [0207] <- 41
```

**Interrupts:** interrupt entry gets a trace line of its own, e.g. `*** interrupt vector 0xffee`. It is followed by the PC/SR pushes as stores, the vector read as a load, and the SP/PC/SR changes.

**Output and performance:** trace output goes to stderr, or to `--trace-file`. It is off by default. When it is off, the cost is a single branch per memory access.

**Check:** golden-output tests compare traces of small hand-encoded programs.

### Step 8 — Loaders

1. Add an ELF32 little-endian loader of about 150 lines, without libelf. It loads `PT_LOAD` segments at their physical addresses, and reads the symbol table so the debugger can accept `break main` and the disassembler can show symbol names.
2. Add an Intel HEX loader.
3. Raw binary loading uses `-b ADDR`.
4. After loading, the reset vector at 0xFFFE gives the initial PC. Replace the hard-coded `pc = 0xC000` in `cpu_reset()`.

### Step 9 — CPU correctness and tests

1. **Test harness:** extend the existing `test/` harness with assertions on the trace log (from Step 7).
2. **Coverage:**
   - all 27 core instructions plus the emulated ones (via the encodings they compile to), in both byte and word modes;
   - all seven addressing modes, including the constant generators (R2/R3), PC-relative, `@PC+` immediates, and SP auto-increment of 2 for byte operations;
   - flags, including V on add/sub overflow, `DADD` BCD and `RRC`/`RRA`/`SXT`;
   - `PUSH`/`CALL`/`RETI` (interrupt entry and `CPUOFF` wake-up are already covered in `test_interrupts.c`).
3. **Cycle counts:** count cycles per instruction according to the format and addressing-mode table in SLAU144, replacing the "4 cycles average". Report the total on exit.
4. **Firmware tests:** when `msp430-elf-gcc` is available, add C test programs under `test/firmware/`. They print through the console UART and end via the stop register, and their output is compared with expected text. The CMake step that builds these is skipped when no toolchain is found.

### Step 10 — Source layout and docs

1. Flatten the tree into `src/` (`cpu/`, `mem/`, `uart/`, `debug/`, `main.c`), plus `test/` and `third_party/`.
2. Rewrite `README.md`: the build, the usage, the console UART register map, the trace format and the debugger commands. Keep the GPL-3.0 notice, the upstream credit and the linenoise license.
3. Update `CLAUDE.md`.

## Commit order

5 → 6 (first usable CLI) → 9 (unit tests, at least the instruction set) → 7 (tracing, using the test harness for golden traces) → 8 → 10.

Each step is one or more commits that leave the tree building.

