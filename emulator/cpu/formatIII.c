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

// ##########+++ Decode Format III Instructions +++#########
// # Format III are jump instructions of the form:
// #   [001C][CCXX][XXXX][XXXX]
// #
// # Where C = Condition, X = 10-bit signed offset
// #
// ########################################################

#include "cpu/formatIII.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cpu/decoder.h"
#include "cpu/registers.h"

void decode_formatIII(Emulator *emu, uint16_t instruction, bool disassemble)
{
    static const char *const names[] = { "JNZ", "JZ", "JNC", "JC", "JN", "JGE", "JL", "JMP" };
    Cpu *cpu                         = emu->cpu;

    uint8_t condition     = (instruction & 0x1C00) >> 10;
    int16_t signed_offset = (instruction & 0x03FF) * 2;
    bool negative         = signed_offset >> 9;

    if (negative) { /* Sign Extend for Arithmetic Operations */
        signed_offset |= 0xF800;
    }

    if (disassemble) {
        Listing listing;

        listing_init(&listing, instruction);
        snprintf(listing.ops, sizeof listing.ops, "0x%04X", (uint16_t)(cpu->pc + signed_offset));
        print_listing(emu, &listing, names[condition]);
        return;
    }

    bool n = cpu->sr & SR_N, v = cpu->sr & SR_V;
    bool taken;

    switch (condition) {
    case 0x0: /* JNE/JNZ: Z = 0 */
        taken = !(cpu->sr & SR_Z);
        break;
    case 0x1: /* JEQ/JZ: Z = 1 */
        taken = cpu->sr & SR_Z;
        break;
    case 0x2: /* JNC/JLO: C = 0 */
        taken = !(cpu->sr & SR_C);
        break;
    case 0x3: /* JC/JHS: C = 1 */
        taken = cpu->sr & SR_C;
        break;
    case 0x4: /* JN: N = 1 */
        taken = n;
        break;
    case 0x5: /* JGE: N .XOR. V = 0 */
        taken = (n == v);
        break;
    case 0x6: /* JL: N .XOR. V = 1 */
        taken = (n != v);
        break;
    default: /* JMP */
        taken = true;
        break;
    }

    /* PC + 2 × offset → PC */
    if (taken)
        cpu->pc += signed_offset;
}
