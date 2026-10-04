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

#ifndef _UTILITIES_H_
#define _UTILITIES_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "emulator.h"

void reg_num_to_name(uint8_t source_reg, char *reg_name);
int8_t reg_name_to_num(char *name);
int load_firmware(Emulator *emu, char *file_name, uint16_t virt_addr);
void display_help(Emulator *emu);

/* Bounded append; truncates instead of overflowing. */
static inline void str_append(char *dst, size_t size, const char *src)
{
    size_t n = strlen(dst);
    if (n + 1 < size)
        snprintf(dst + n, size - n, "%s", src);
}

#endif
