#ifndef _EMULATOR_H_
#define _EMULATOR_H_

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct Cpu Cpu;
typedef struct Debugger Debugger;
typedef struct Trace Trace;

/* Why the emulator stopped. */
typedef enum {
    EMU_RUNNING,    /* not stopped */
    EMU_PROGRAM,    /* the stop register was written; see exit_code */
    EMU_MAX_CYCLES, /* the cycle limit was reached */
    EMU_ILLEGAL,    /* illegal instruction; PC points at it */
    EMU_SLEEP,      /* CPUOFF with no possible wake-up */
    EMU_BREAKPOINT, /* PC reached a breakpoint */
    EMU_INTERRUPT,  /* break_request from the front-end */
} StopReason;

typedef struct Emulator {
    Cpu *cpu;
    Debugger *debugger;
    uint8_t mem[0x10000]; /* 64 KB address space; the CPU goes through memory/memory.h */

    StopReason stop;
    uint16_t exit_code;                  /* value written to the stop register */
    volatile sig_atomic_t break_request; /* set by the front-end, e.g. on SIGINT */

    uint64_t uart_poll_at; /* cycle of the next console input poll */
    bool uart_eof;         /* console input is exhausted */

    bool trace;       /* tracing enabled; see debugger/trace.h */
    FILE *trace_file; /* trace output; NULL means stderr */
    Trace *tracer;
} Emulator;

/* Allocate an emulator in its reset state. */
Emulator *emu_create(void);
void emu_destroy(Emulator *emu);

/* Reset the CPU and the devices; memory is kept. */
void emu_reset(Emulator *emu);

/*
 * Run until something stops the CPU: the stop register, an illegal
 * instruction, a breakpoint, a break request (consumed), sleeping with no
 * possible wake-up, or cpu->cycles reaching max_cycles (0 means no limit).
 * A breakpoint at the starting PC does not stop the run.
 */
StopReason emu_run(Emulator *emu, uint64_t max_cycles);

/* Short description, e.g. "Illegal instruction". */
const char *emu_stop_reason(StopReason reason);

#endif
