#include "memory/memory.h"

#include <stdbool.h>
#include <stdint.h>

#include "cpu/registers.h"
#include "uart/uart.h"

/*
 * All 64 KB is plain RAM, except the console UART registers and the
 * stop register, which are dispatched to their handlers.
 */
#define IO_END 0x0200 /* devices live below this */

/* A write here stops the simulator; the value is the exit code. */
#define STOP_REG 0x01FE

static uint8_t io_read(Emulator *emu, uint16_t addr)
{
    switch (addr) {
    case IFG2:
    case UCA0RXBUF:
        return uart_read(emu, addr);
    default:
        return emu->mem[addr];
    }
}

static void io_write(Emulator *emu, uint16_t addr, uint8_t val)
{
    switch (addr) {
    case IE2:
    case IFG2:
    case UCA0RXBUF:
    case UCA0TXBUF:
        uart_write(emu, addr, val);
        break;
    default:
        emu->mem[addr] = val;
        break;
    }
}

static void stop(Emulator *emu, uint16_t code)
{
    emu->stopped      = true;
    emu->exit_code    = code;
    emu->cpu->running = false;
}

uint16_t mem_read(Emulator *emu, uint16_t addr, int size, Access kind)
{
    bool io = (addr < IO_END && kind == ACC_DATA);

    if (size == 1)
        return io ? io_read(emu, addr) : emu->mem[addr];
    addr &= ~1;
    if (io)
        return io_read(emu, addr) | (io_read(emu, addr + 1) << 8);
    return emu->mem[addr] | (emu->mem[addr + 1] << 8);
}

void mem_write(Emulator *emu, uint16_t addr, uint16_t val, int size)
{
    if (size == 2)
        addr &= ~1;
    if (addr >= IO_END) {
        emu->mem[addr] = val;
        if (size == 2)
            emu->mem[addr + 1] = val >> 8;
        return;
    }
    if (addr == STOP_REG) {
        stop(emu, size == 1 ? (val & 0xFF) : val);
        return;
    }
    io_write(emu, addr, val);
    if (size == 2)
        io_write(emu, addr + 1, val >> 8);
}
