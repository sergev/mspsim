#include "debugger/trace.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cpu/interrupts.h"
#include "cpu/registers.h"
#include "debugger/disassembler.h"
#include "utilities.h"

static FILE *trace_out(Emulator *emu)
{
    return emu->trace_file ? emu->trace_file : stderr;
}

void trace_access(Emulator *emu, bool write, int size, uint16_t addr, uint16_t val)
{
    Trace *t = emu->tracer;

    if (t->count == TRACE_MAX_ACCESSES)
        return;
    t->log[t->count++] = (TraceAccess){
        .write = write, .size = size, .addr = addr, .val = size == 1 ? (val & 0xFF) : val
    };
}

void trace_snapshot(Emulator *emu)
{
    Trace *t = emu->tracer;

    memcpy(t->regs, emu->cpu->r, sizeof t->regs);
    t->count = 0;
}

void trace_begin(Emulator *emu)
{
    trace_snapshot(emu);
    disassemble_at(emu, emu->cpu->pc, &emu->tracer->listing);
}

/* Loads and stores, then changed registers. */
static void print_effects(Emulator *emu, bool show_pc)
{
    static const char *const names[16] = { "PC", "SP", "SR",  "R3",  "R4",  "R5",  "R6",  "R7",
                                           "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15" };
    static const struct {
        uint16_t mask;
        const char *name;
    } flags[] = {
        { SR_V, "V" },           { SR_SCG1, "SCG1" },     { SR_SCG0, "SCG0" },
        { SR_OSCOFF, "OSCOFF" }, { SR_CPUOFF, "CPUOFF" }, { SR_GIE, "GIE" },
        { SR_N, "N" },           { SR_Z, "Z" },           { SR_C, "C" },
    };
    Trace *t  = emu->tracer;
    FILE *out = trace_out(emu);

    for (int i = 0; i < t->count; i++) {
        TraceAccess *a = &t->log[i];

        fprintf(out, "      %-6s [%04x] = %0*x\n",
                a->size == 1 ? (a->write ? "Writeb" : "Readb") : (a->write ? "Write" : "Read"),
                a->addr, a->size * 2, a->val);
    }
    for (int r = show_pc ? 0 : 1; r < 16; r++) {
        uint16_t old = t->regs[r], new = emu->cpu->r[r];

        if (old == new)
            continue;
        fprintf(out, "      %s = %04x", names[r], new);
        if (r == 2) {
            char decoded[64] = "";

            for (size_t f = 0; f < sizeof flags / sizeof *flags; f++) {
                if (new & flags[f].mask) {
                    str_append(decoded, sizeof decoded, decoded[0] ? " " : "");
                    str_append(decoded, sizeof decoded, flags[f].name);
                }
            }
            if (decoded[0])
                fprintf(out, " [%s]", decoded);
        }
        fputc('\n', out);
    }
}

void trace_end(Emulator *emu)
{
    char line[160];

    listing_text(emu, &emu->tracer->listing, line, sizeof line);
    fprintf(trace_out(emu), "%s\n", line);
    print_effects(emu, false);
}

void trace_interrupt(Emulator *emu, int irq)
{
    fprintf(trace_out(emu), "*** interrupt vector 0x%04x\n", VECTOR_TABLE + 2 * irq);
    print_effects(emu, true);
}
