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

#ifndef _DISASSEMBLER_H_
#define _DISASSEMBLER_H_

#include <stddef.h>
#include <stdint.h>

#include "cpu/decoder.h"
#include "emulator.h"

/* Disassemble the instruction at addr into l; returns the address after it. */
uint16_t disassemble_at(Emulator *emu, uint16_t addr, Listing *l);

/* The listing line, with jump/call and address targets annotated by symbol. */
void listing_text(Emulator *emu, const Listing *l, char *buf, size_t size);

/* Print times instructions from start_addr, with a "name:" line at each symbol. */
void disassemble(Emulator *emu, uint16_t start_addr, uint32_t times);

#endif
