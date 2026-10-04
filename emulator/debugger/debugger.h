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

#ifndef _DEBUGGER_H_
#define _DEBUGGER_H_

#include <stdbool.h>
#include <stdint.h>

#include "emulator.h"

typedef enum { BYTE_STRIDE, WORD_STRIDE, DWORD_STRIDE } Stride;
enum { MAX_BREAKPOINTS = 100 };

typedef struct Debugger {
    bool color; /* ANSI colours in the register display */

    uint16_t bp_addresses[MAX_BREAKPOINTS];
    uint32_t num_bps;
} Debugger;

/* What the front-end does after a command. */
typedef enum {
    DBG_STAY, /* read the next command */
    DBG_RUN,  /* run until something stops the CPU */
    DBG_QUIT,
} DebugAction;

void setup_debugger(Emulator *emu);

void dump_memory(Emulator *emu, uint16_t start_addr, uint8_t stride);

DebugAction exec_cmd(Emulator *emu, const char *line);

/* Index of the breakpoint at addr, or -1. */
int breakpoint_at(Emulator *emu, uint16_t addr);

/* Print why the CPU stopped (emu->stop), then the registers and next instruction. */
void report_stop(Emulator *emu);

#endif
