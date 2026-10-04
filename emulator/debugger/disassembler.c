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

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cpu/decoder.h"
#include "cpu/registers.h"
#include "io.h"
#include "loader/symbols.h"

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

void listing_text(Emulator *emu, const Listing *l, char *buf, size_t size)
{
    format_listing(l, buf, size);
    for (int i = 0; i < l->ntargets; i++) {
        uint16_t addr   = l->targets[i];
        const Symbol *s = l->code[i] ? symbol_before(emu, addr) : symbol_at(emu, addr);
        size_t n        = strlen(buf);

        if (s == NULL || addr - s->addr >= 0x1000)
            continue;
        if (addr == s->addr)
            snprintf(buf + n, size - n, " <%s>", s->name);
        else
            snprintf(buf + n, size - n, " <%s+0x%x>", s->name, addr - s->addr);
    }
}

void disassemble(Emulator *emu, uint16_t start_addr, uint32_t times)
{
    uint16_t addr = start_addr;

    for (uint32_t i = 0; i < times; i++) {
        const Symbol *s = symbol_at(emu, addr);
        Listing l;
        char line[160];

        if (s != NULL)
            emu_printf(emu, "%s:\n", s->name);
        addr = disassemble_at(emu, addr, &l);
        listing_text(emu, &l, line, sizeof line);
        emu_printf(emu, "%s\n", line);
    }
}
