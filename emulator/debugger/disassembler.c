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

#include "debugger/disassembler.h"

#include <stdint.h>

#include "cpu/decoder.h"
#include "cpu/registers.h"
#include "io.h"

uint16_t disassemble_at(Emulator *emu, uint16_t addr, Listing *l)
{
    Cpu *cpu          = emu->cpu;
    uint16_t saved_pc = cpu->pc, next;

    cpu->pc = addr;
    listing_init(l, addr, fetch(emu));
    decode(emu, l->words[0], l);
    next    = cpu->pc;
    cpu->pc = saved_pc;
    return next;
}

void disassemble(Emulator *emu, uint16_t start_addr, uint32_t times)
{
    uint16_t addr = start_addr;

    for (uint32_t i = 0; i < times; i++) {
        Listing l;
        char line[128];

        addr = disassemble_at(emu, addr, &l);
        format_listing(&l, line, sizeof line);
        emu_printf(emu, "%s\n", line);
    }
}
