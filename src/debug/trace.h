#ifndef _TRACE_H_
#define _TRACE_H_

#include <stdbool.h>
#include <stdint.h>

#include "cpu/decoder.h"
#include "emulator.h"

/*
 * Execution trace, enabled by emu->trace: one line per instruction, then
 * its data loads and stores in order, then the registers it changed
 * (not PC). Output goes to emu->trace_file, or stderr.
 */

typedef struct {
    bool write;
    uint8_t size; /* 1 or 2 bytes */
    uint16_t addr;
    uint16_t val;
} TraceAccess;

enum { TRACE_MAX_ACCESSES = 16 };

typedef struct Trace {
    uint16_t regs[16]; /* before the instruction */
    Listing listing;
    TraceAccess log[TRACE_MAX_ACCESSES];
    int count;
} Trace;

/* Called by the memory bus for each ACC_DATA access while tracing. */
void trace_access(Emulator *emu, bool write, int size, uint16_t addr, uint16_t val);

/* Snapshot registers and clear the access log. */
void trace_snapshot(Emulator *emu);

/* Around an instruction at PC: snapshot and disassemble, then print. */
void trace_begin(Emulator *emu);
void trace_end(Emulator *emu);

/* After interrupt entry through vector irq; PC changes are shown too. */
void trace_interrupt(Emulator *emu, int irq);

#endif
