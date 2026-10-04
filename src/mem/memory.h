#ifndef _MEMORY_H_
#define _MEMORY_H_

#include <stdint.h>

#include "emulator.h"

/* The memory bus: the only way the CPU touches memory. */

typedef enum {
    ACC_FETCH, /* opcode and extension words */
    ACC_DATA,  /* operand loads, stack, vectors */
} Access;

/* size is 1 or 2 bytes; word accesses ignore address bit 0. */
uint16_t mem_read(Emulator *emu, uint16_t addr, int size, Access kind);
void mem_write(Emulator *emu, uint16_t addr, uint16_t val, int size);

#endif
