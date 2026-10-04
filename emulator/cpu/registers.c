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

// ##########+++ MSP430 Register initialization +++##########
void initialize_msp_registers(Emulator *emu)
{
    cpu_reset(emu);
    emu->cpu->running = false;
}

// ##########+++ Set SR struct Value +++##########
void set_sr_value(Emulator *emu, uint16_t value)
{
    Cpu *cpu = emu->cpu;

    // reset SR to set it properly...
    memset(&cpu->sr, 0, sizeof(Status_reg));
    // memcpy(&cpu->sr, &value, 16);

    if (value & 0x8000)
        cpu->sr.reserved |= 0x8000;
    if (value & 0x4000)
        cpu->sr.reserved |= 0x4000;
    if (value & 0x2000)
        cpu->sr.reserved |= 0x2000;
    if (value & 0x1000)
        cpu->sr.reserved |= 0x1000;
    if (value & 0x0800)
        cpu->sr.reserved |= 0x0800;
    if (value & 0x0400)
        cpu->sr.reserved |= 0x0400;
    if (value & 0x0200)
        cpu->sr.reserved |= 0x0200;

    cpu->sr.overflow = (value & 0x0100) ? 1 : 0;
    cpu->sr.SCG1     = (value & 0x0080) ? 1 : 0;
    cpu->sr.SCG0     = (value & 0x0040) ? 1 : 0;
    cpu->sr.OSCOFF   = (value & 0x0020) ? 1 : 0;
    cpu->sr.CPUOFF   = (value & 0x0010) ? 1 : 0;
    cpu->sr.GIE      = (value & 0x0008) ? 1 : 0;
    cpu->sr.negative = (value & 0x0004) ? 1 : 0;
    cpu->sr.zero     = (value & 0x0002) ? 1 : 0;
    cpu->sr.carry    = (value & 0x0001) ? 1 : 0;
}

// ##########+++ Return value from SR struct +++##########
uint16_t sr_to_value(Emulator *emu)
{
    Cpu *cpu    = emu->cpu;
    uint16_t r2 = 0;

    // reserved bits not working quite right yet
    if (cpu->sr.reserved & 0b1000000) {
        r2 |= 0x8000;
    }
    if (cpu->sr.reserved & 0b0100000) {
        r2 |= 0x4000;
    }
    if (cpu->sr.reserved & 0b0010000) {
        r2 |= 0x2000;
    }
    if (cpu->sr.reserved & 0b0001000) {
        r2 |= 0x1000;
    }
    if (cpu->sr.reserved & 0b0000100) {
        r2 |= 0x0800;
    }
    if (cpu->sr.reserved & 0b0000010) {
        r2 |= 0x0400;
    }
    if (cpu->sr.reserved & 0b0000001) {
        r2 |= 0x0200;
    }

    if (cpu->sr.overflow) {
        r2 |= 0x0100;
    }

    if (cpu->sr.SCG1) {
        r2 |= 0x0080;
    }

    if (cpu->sr.SCG0) {
        r2 |= 0x0040;
    }

    if (cpu->sr.OSCOFF) {
        r2 |= 0x0020;
    }

    if (cpu->sr.CPUOFF) {
        r2 |= 0x0010;
    }

    if (cpu->sr.GIE) {
        r2 |= 0x0008;
    }

    if (cpu->sr.negative) {
        r2 |= 0x0004;
    }

    if (cpu->sr.zero) {
        r2 |= 0x0002;
    }

    if (cpu->sr.carry) {
        r2 |= 0x0001;
    }

    return r2;
}

void cpu_step(Emulator *emu)
{
    Cpu *cpu = emu->cpu;

    if (!cpu->sr.CPUOFF) {
        decode(emu, fetch(emu), EXECUTE);
        cpu->cycles += 4; /* average; exact counts in Plan step 9 */
    } else {
        cpu->cycles += 1;
    }
    if (handle_interrupts(emu))
        cpu->cycles += 6;
}

void cpu_reset(Emulator *emu)
{
    Cpu *cpu = emu->cpu;

    cpu->pc     = 0xC000;
    cpu->sp     = 0x400;
    cpu->cycles = 0;

    memset(&cpu->sr, 0, sizeof(Status_reg));
    cpu->cg2 = cpu->r4 = cpu->r5 = cpu->r6 = cpu->r7 = cpu->r8 = cpu->r9 = cpu->r10 = cpu->r11 =
        cpu->r12 = cpu->r13 = cpu->r14 = cpu->r15 = 0;
    cpu->irq_pending                              = 0;
}