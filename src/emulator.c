#include "emulator.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cpu/interrupts.h"
#include "cpu/registers.h"
#include "debug/debugger.h"
#include "debug/trace.h"
#include "hostio/hostio.h"
#include "loader/symbols.h"
#include "uart/uart.h"

Emulator *emu_create(void)
{
    Emulator *emu = calloc(1, sizeof(Emulator));
    emu->cpu      = calloc(1, sizeof(Cpu));
    emu->debugger = calloc(1, sizeof(Debugger));
    emu->tracer   = calloc(1, sizeof(Trace));
    setup_debugger(emu);

    /* Info memory and the code area read as erased flash. */
    memset(emu->mem + 0x1000, 0xFF, 0x100);
    memset(emu->mem + 0xC000, 0xFF, 0x4000);
    emu->entry = -1;

    emu_reset(emu);
    return emu;
}

void emu_reset(Emulator *emu)
{
    cpu_reset(emu);
    uart_reset(emu);
    hostio_reset(emu);
    emu->stop      = EMU_RUNNING;
    emu->exit_code = 0;
}

void emu_destroy(Emulator *emu)
{
    if (emu == NULL)
        return;
    free(emu->cpu);
    free(emu->debugger);
    free(emu->tracer);
    symbols_free(emu);
    free(emu);
}

/* An interrupt would be accepted after this step. */
static bool irq_deliverable(Cpu *cpu)
{
    uint16_t pending = cpu->irq_pending;

    if (!(cpu->sr & SR_GIE))
        pending &= 1u << NMI_IRQ;
    return pending != 0;
}

StopReason emu_run(Emulator *emu, uint64_t max_cycles)
{
    Cpu *cpu = emu->cpu;

    emu->stop = EMU_RUNNING;
    while (emu->stop == EMU_RUNNING) {
        if (emu->break_request) {
            emu->break_request = 0;
            emu->stop          = EMU_INTERRUPT;
            break;
        }
        if (max_cycles && cpu->cycles >= max_cycles) {
            emu->stop = EMU_MAX_CYCLES;
            break;
        }
        if ((cpu->sr & SR_CPUOFF) && !irq_deliverable(cpu)) {
            if (!(cpu->sr & SR_GIE) || !uart_may_interrupt(emu)) {
                emu->stop = EMU_SLEEP;
                break;
            }
            /* Nothing happens until the next input poll. */
            uint64_t wake = emu->uart_poll_at;
            if (max_cycles && wake > max_cycles)
                wake = max_cycles;
            if (wake > cpu->cycles)
                cpu->cycles = wake;
        }
        cpu_step(emu);
        if (emu->stop == EMU_RUNNING && breakpoint_at(emu, cpu->pc) >= 0)
            emu->stop = EMU_BREAKPOINT;
    }
    return emu->stop;
}

const char *emu_stop_reason(StopReason reason)
{
    switch (reason) {
    case EMU_RUNNING:
        return "Running";
    case EMU_PROGRAM:
        return "Stopped by program";
    case EMU_MAX_CYCLES:
        return "Cycle limit reached";
    case EMU_ILLEGAL:
        return "Illegal instruction";
    case EMU_SLEEP:
        return "CPU off with no wake-up source";
    case EMU_BREAKPOINT:
        return "Breakpoint";
    case EMU_INTERRUPT:
        return "Interrupted";
    }
    return "?";
}
