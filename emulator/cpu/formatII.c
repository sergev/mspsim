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

// ##########+++ Decode Format II Instructions +++#########
// # Format II are single operand of the form:
// #   [0001][00CC][CBAA][SSSS]
// #
// # Where C = Opcode, B = Byte/Word flag,
// #       A = Addressing mode for source
// #       S = Source
// ########################################################

#include "cpu/formatII.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cpu/decoder.h"
#include "cpu/flag_handler.h"
#include "cpu/registers.h"
#include "memory/memory.h"

void decode_formatII(Emulator *emu, uint16_t instruction, Listing *l)
{
    static const char *const names[] = { "rrc", "swpb", "rra", "sxt", "push", "call", "reti" };
    Cpu *cpu                         = emu->cpu;

    uint8_t opcode  = (instruction & 0x0380) >> 7;
    uint8_t bw_flag = (instruction & 0x0040) >> 6;
    uint8_t as_flag = (instruction & 0x0030) >> 4;
    uint8_t source  = (instruction & 0x000F);
    bool byte       = (bw_flag == EMU_BYTE);

    Operand op;
    uint16_t value;

    if (opcode == 6) { /* RETI has no operand */
        if (l)
            snprintf(l->mnemonic, sizeof l->mnemonic, "reti");
    } else if (opcode == 7) {
        if (l) {
            snprintf(l->mnemonic, sizeof l->mnemonic, ".word");
            snprintf(l->ops, sizeof l->ops, "0x%04x", instruction);
        }
    } else {
        decode_operand(emu, l, &op, source, as_flag, byte, true);
        /* SWPB, SXT and CALL have no byte form */
        if (l)
            snprintf(l->mnemonic, sizeof l->mnemonic, "%s%s", names[opcode],
                     (byte && !(opcode & 1)) ? ".b" : "");
    }
    if (l)
        return;

    switch (opcode) {
        /*  RRC Rotate right through carry
         *    C → MSB → MSB-1 .... LSB+1 → LSB → C
         *
         *  Description The destination operand is shifted right one position
         *  as shown in Figure 3-18. The carry bit (C) is shifted into the MSB,
         *  the LSB is shifted into the carry bit (C).
         *
         * N: Set if result is negative, reset if positive
         * Z: Set if result is zero, reset otherwise
         * C: Loaded from the LSB
         * V: Reset
         */
    case 0x0: {
        bool CF = cpu->sr & SR_C;

        value = operand_read(emu, &op, byte);
        set_flag(cpu, SR_C, value & 1); /* Set CF from LSB */
        value >>= 1;                    /* Shift one right */
        if (CF)                         /* Set MSB from prev CF */
            value |= byte ? 0x80 : 0x8000;
        operand_write(emu, &op, value, byte);

        set_flag(cpu, SR_Z, is_zero(value, bw_flag));
        set_flag(cpu, SR_N, is_negative(value, bw_flag));
        cpu->sr &= ~SR_V;
        break;
    }

        /* SWPB Swap bytes
         * bw flag always 0 (word)
         * Bits 15 to 8 ↔ bits 7 to 0
         */
    case 0x1: {
        value = operand_read(emu, &op, false);
        operand_write(emu, &op, (uint16_t)(value << 8 | value >> 8), false);
        break;
    }

        /* RRA Rotate right arithmetic
         *   MSB → MSB, MSB → MSB-1, ... LSB+1 → LSB, LSB → C
         *
         * N: Set if result is negative, reset if positive
         * Z: Set if result is zero, reset otherwise
         * C: Loaded from the LSB
         * V: Reset
         */
    case 0x2: {
        uint16_t msb = byte ? 0x80 : 0x8000;

        value = operand_read(emu, &op, byte);
        set_flag(cpu, SR_C, value & 1);
        value = (value >> 1) | (value & msb); /* Extend Sign */
        operand_write(emu, &op, value, byte);

        set_flag(cpu, SR_Z, is_zero(value, bw_flag));
        set_flag(cpu, SR_N, is_negative(value, bw_flag));
        cpu->sr &= ~SR_V;
        break;
    }

        /* SXT Sign extend byte to word
         *   bw flag always 0 (word)
         *
         * Bit 7 → Bit 8 ......... Bit 15
         *
         * N: Set if result is negative, reset if positive
         * Z: Set if result is zero, reset otherwise
         * C: Set if result is not zero, reset otherwise (.NOT. Zero)
         * V: Reset
         */

    case 0x3: {
        value = (int16_t)(int8_t)operand_read(emu, &op, false);
        operand_write(emu, &op, value, false);

        set_flag(cpu, SR_N, is_negative(value, EMU_WORD));
        set_flag(cpu, SR_Z, value == 0);
        set_flag(cpu, SR_C, value != 0);
        cpu->sr &= ~SR_V;
        break;
    }

        /* PUSH push value on to the stack
         *
         *   SP - 2 → SP
         *   src → @SP
         *
         */
    case 0x4: {
        value = operand_read(emu, &op, byte);
        cpu->sp -= 2; /* Yes, even for BYTE Instructions */
        mem_write(emu, cpu->sp, value, byte ? 1 : 2);
        break;
    }

        /* CALL SUBROUTINE:
         *     PUSH PC and PC = SRC
         *
         *     This is always a word instruction. Supporting all addressing modes
         */

    case 0x5: {
        value = operand_read(emu, &op, false);
        cpu->sp -= 2;
        mem_write(emu, cpu->sp, cpu->pc, 2);
        cpu->pc = value;
        break;
    }

        // # RETI Return from interrupt: Pop SR then pop PC
    case 0x6: {
        cpu->sr = mem_read(emu, cpu->sp, 2, ACC_DATA);
        cpu->sp += 2;
        cpu->pc = mem_read(emu, cpu->sp, 2, ACC_DATA);
        cpu->sp += 2;
        break;
    }
    default: {
        emu->stop = EMU_ILLEGAL;
    }

    } // # End of Switch
}
