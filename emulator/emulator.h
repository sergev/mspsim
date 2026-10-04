#ifndef _EMULATOR_H_
#define _EMULATOR_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct Cpu Cpu;
typedef struct Debugger Debugger;

typedef struct Emulator {
    Cpu *cpu;
    Debugger *debugger;
    uint8_t mem[0x10000]; /* 64 KB address space; the CPU goes through memory/memory.h */

    bool stopped;          /* the stop register was written */
    uint16_t exit_code;    /* value written to the stop register */
    uint64_t uart_poll_at; /* cycle of the next console input poll */
} Emulator;

/* Allocate an emulator in its reset state, with the debugger paused. */
Emulator *emu_create(void);
void emu_destroy(Emulator *emu);

/* Reset the CPU and the devices; memory is kept. */
void emu_reset(Emulator *emu);

#endif
