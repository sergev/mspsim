/*
  MSP430 Emulator
  Copyright (C) 2020 Rudolf Geosits (rgeosits@live.esu.edu)

  "MSP430 Emulator" is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  "MSP430 Emulator" is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "cpu/registers.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cpu/decoder.h"
#include "cpu/interrupts.h"
#include "debug/trace.h"
#include "hostio/hostio.h"
#include "uart/uart.h"

void reg_write(Cpu *cpu, unsigned reg, uint16_t val, bool byte)
{
    if (reg == 3)
        return;
    if (byte)
        val &= 0xFF;
    if (reg == 2)
        val &= SR_MASK;
    cpu->r[reg] = val;
}

void cpu_step(Emulator *emu)
{
    Cpu *cpu = emu->cpu;
    int irq;

    uart_tick(emu);
    if (!(cpu->sr & SR_CPUOFF) && hostio_is_syscall(cpu->pc)) {
        cpu->cycles += hostio_syscall(emu);
    } else if (!(cpu->sr & SR_CPUOFF)) {
        uint16_t pc = cpu->pc;

        if (pc == emu->cio_hook)
            hostio_cio(emu);
        if (emu->trace)
            trace_begin(emu);
        unsigned cycles = decode(emu, fetch(emu), NULL);
        if (emu->stop == EMU_ILLEGAL) {
            cpu->pc = pc;
            return;
        }
        cpu->cycles += cycles;
        if (emu->trace)
            trace_end(emu);
    } else {
        cpu->cycles += 1; /* while CPUOFF, one cycle per step */
    }

    if (emu->trace)
        trace_snapshot(emu);
    irq = handle_interrupts(emu);
    if (irq >= 0) {
        cpu->cycles += 6; /* interrupt acceptance */
        if (emu->trace)
            trace_interrupt(emu, irq);
    }
}

void cpu_reset(Emulator *emu)
{
    Cpu *cpu = emu->cpu;

    uint16_t vector = emu->mem[0xFFFE] | emu->mem[0xFFFF] << 8;

    memset(cpu->r, 0, sizeof cpu->r);
    cpu->pc          = (vector == 0xFFFF && emu->entry >= 0) ? emu->entry : vector;
    cpu->sp          = 0x400;
    cpu->cycles      = 0;
    cpu->irq_pending = 0;
}
