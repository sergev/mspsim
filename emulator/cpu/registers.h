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

#ifndef _REGISTERS_H_
#define _REGISTERS_H_

#include <stdbool.h>
#include <stdint.h>

#include "emulator.h"

/* SR (R2) bits */
enum {
    SR_C      = 0x0001, /* carry */
    SR_Z      = 0x0002, /* zero */
    SR_N      = 0x0004, /* negative */
    SR_GIE    = 0x0008, /* maskable interrupts enabled */
    SR_CPUOFF = 0x0010,
    SR_OSCOFF = 0x0020,
    SR_SCG0   = 0x0040,
    SR_SCG1   = 0x0080,
    SR_V      = 0x0100, /* overflow */
};

typedef struct Cpu {
    bool running; /* CPU running or not */

    union {
        uint16_t r[16]; /* R0-R15 */
        struct {
            uint16_t pc, sp, sr, cg2;
            uint16_t r4, r5, r6, r7;
            uint16_t r8, r9, r10, r11;
            uint16_t r12, r13, r14, r15;
        };
    };

    uint16_t irq_pending; /* bit N: request for vector at 0xFFE0 + 2*N */
    uint64_t cycles;      /* CPU cycles since reset */
} Cpu;

static inline void set_flag(Cpu *cpu, uint16_t mask, bool on)
{
    if (on)
        cpu->sr |= mask;
    else
        cpu->sr &= ~mask;
}

/* Register-mode write: byte writes clear the high byte; R3 discards writes. */
void reg_write(Cpu *cpu, unsigned reg, uint16_t val, bool byte);

void cpu_step(Emulator *emu);
void cpu_reset(Emulator *emu);

#endif
