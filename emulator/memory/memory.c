#include "memory/memory.h"

#include <stdint.h>

/*
 * All 64 KB is plain RAM. The console UART registers (0x0001, 0x0003,
 * 0x0066, 0x0067) and the stop register (0x01FE) get dispatched here
 * in Plan Step 5.
 */

uint16_t mem_read(Emulator *emu, uint16_t addr, int size, Access kind)
{
    (void)kind;
    if (size == 1)
        return emu->mem[addr];
    addr &= ~1;
    return emu->mem[addr] | (emu->mem[addr + 1] << 8);
}

void mem_write(Emulator *emu, uint16_t addr, uint16_t val, int size)
{
    if (size == 1) {
        emu->mem[addr] = val;
        return;
    }
    addr &= ~1;
    emu->mem[addr]     = val;
    emu->mem[addr + 1] = val >> 8;
}
