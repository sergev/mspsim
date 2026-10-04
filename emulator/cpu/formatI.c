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

// ##########+++ Decode Format I Instructions +++##########
// # Format I are double operand of the form:
// # [CCCC][SSSS][ABaa][DDDD]
// #
// # Where C = Opcode, B = Byte/Word flag,
// # A = Addressing mode for destination
// # a = Addressing mode for s_reg_name
// # S = S_Reg_Name, D = Destination
// ########################################################

#include "cpu/formatI.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cpu/decoder.h"
#include "cpu/flag_handler.h"
#include "cpu/registers.h"
#include "utilities.h"

void decode_formatI(Emulator *emu, uint16_t instruction, Listing *l)
{
    static const char *const names[] = { "mov",  "add", "addc", "subc", "sub", "cmp",
                                         "dadd", "bit", "bic",  "bis",  "xor", "and" };
    Cpu *cpu                         = emu->cpu;

    uint8_t opcode      = (instruction & 0xF000) >> 12;
    uint8_t source      = (instruction & 0x0F00) >> 8;
    uint8_t as_flag     = (instruction & 0x0030) >> 4;
    uint8_t destination = (instruction & 0x000F);
    uint8_t ad_flag     = (instruction & 0x0080) >> 7;
    uint8_t bw_flag     = (instruction & 0x0040) >> 6;
    bool byte           = (bw_flag == EMU_BYTE);
    uint16_t mask       = byte ? 0xFF : 0xFFFF;

    Operand src, dst;
    uint16_t source_value = 0, dst_value = 0, result;

    /* Source is read before the destination's extension word is fetched. */
    decode_operand(emu, l, &src, source, as_flag, byte, true);
    if (!l)
        source_value = operand_read(emu, &src, byte);
    else
        str_append(l->ops, sizeof l->ops, ", ");
    decode_operand(emu, l, &dst, destination, ad_flag, byte, false);

    if (l) {
        snprintf(l->mnemonic, sizeof l->mnemonic, "%s%s", names[opcode - 4], byte ? ".b" : "");
        return;
    }

    bool dst_is_sr = (dst.kind == OPND_REG && dst.reg == 2);

    if (opcode != 0x4)
        dst_value = operand_read(emu, &dst, byte);

    switch (opcode) {
    /* MOV SOURCE, DESTINATION
     *   Ex: MOV #4, R6
     *
     * SOURCE = DESTINATION
     *
     * The source operand is moved to the destination. The source operand is
     * not affected. The previous contents of the destination are lost.
     *
     */
    case 0x4: {
        operand_write(emu, &dst, source_value, byte);
        break;
    }

    /* ADD SOURCE, DESTINATION
     *   Ex: ADD R5, R4
     *
     * The source operand is added to the destination operand. The source op
     * is not affected. The previous contents of the destination are lost.
     *
     * DESTINATION = SOURCE + DESTINATION
     *
     * N: Set if result is negative, reset if positive
     * Z: Set if result is zero, reset otherwise
     * C: Set if there is a carry from the result, cleared if not
     * V: Set if an arithmetic overflow occurs, otherwise reset
     *
     */
    case 0x5: {
        result = (dst_value + source_value) & mask;
        operand_write(emu, &dst, result, byte);

        set_flag(cpu, SR_Z, is_zero(result, bw_flag));
        set_flag(cpu, SR_N, is_negative(result, bw_flag));
        set_flag(cpu, SR_C, is_carried(dst_value, source_value, bw_flag));
        set_flag(cpu, SR_V, is_overflowed(source_value, dst_value, result, bw_flag));
        break;
    }

    /* ADDC SOURCE, DESTINATION
     *   Ex: ADDC R5, R4
     *
     * DESTINATION += (SOURCE + C)
     *
     * N: Set if result is negative, reset if positive
     * Z: Set if result is zero, reset otherwise
     * C: Set if there is a carry from the result, cleared if not
     * V: Set if an arithmetic overflow occurs, otherwise reset
     *
     */
    case 0x6: {
        result = (dst_value + source_value + (cpu->sr & SR_C)) & mask;
        operand_write(emu, &dst, result, byte);

        set_flag(cpu, SR_Z, is_zero(result, bw_flag));
        set_flag(cpu, SR_N, is_negative(result, bw_flag));
        set_flag(cpu, SR_C, is_carried(dst_value, source_value, bw_flag));
        set_flag(cpu, SR_V, is_overflowed(source_value, dst_value, result, bw_flag));
        break;
    }

    /* SUBC SOURCE, DESTINATION
     *   Ex: SUB R4, R5
     *
     *   DST += ~SRC + C
     *
     *  N: Set if result is negative, reset if positive
     *  Z: Set if result is zero, reset otherwise
     *  C: Set if there is a carry from the MSB of the result, reset otherwise.
     *     Set to 1 if no borrow, reset if borrow.
     *  V: Set if an arithmetic overflow occurs, otherwise reset
     *
     *
     */
    case 0x7: {
        source_value = ~source_value & mask; /* 1's comp */
        result       = (dst_value + source_value + (cpu->sr & SR_C)) & mask;
        operand_write(emu, &dst, result, byte);

        set_flag(cpu, SR_Z, is_zero(result, bw_flag));
        set_flag(cpu, SR_N, is_negative(result, bw_flag));
        set_flag(cpu, SR_C, is_carried(dst_value, source_value, bw_flag));
        set_flag(cpu, SR_V, is_overflowed(source_value, dst_value, result, bw_flag));
        break;
    }

        /* SUB SOURCE, DESTINATION
         *   Ex: SUB R4, R5
         *
         *   DST -= SRC
         *
         *  N: Set if result is negative, reset if positive
         *  Z: Set if result is zero, reset otherwise
         *  C: Set if there is a carry from the MSB of the result, reset otherwise.
         *     Set to 1 if no borrow, reset if borrow.
         *  V: Set if an arithmetic overflow occurs, otherwise reset
         *  TODO: SUBTRACTION OVERFLOW FLAG ERROR
         *  TODO: C is never cleared
         */

    case 0x8: {
        uint16_t negated = (~source_value + 1) & mask;

        result = (dst_value + negated) & mask;
        operand_write(emu, &dst, result, byte);

        set_flag(cpu, SR_Z, is_zero(result, bw_flag));
        set_flag(cpu, SR_N, is_negative(result, bw_flag));
        if (source_value == 0 || is_carried(dst_value, negated, bw_flag))
            cpu->sr |= SR_C;
        set_flag(cpu, SR_V, is_overflowed(negated, dst_value, result, bw_flag));
        break;
    }

    /* CMP SOURCE, DESTINATION
     *
     * N: Set if result is negative, reset if positive (src ≥ dst)
     * Z: Set if result is zero, reset otherwise (src = dst)
     * C: Set if there is a carry from the MSB of the result, reset otherwise
     * V: Set if an arithmetic overflow occurs, otherwise reset
     * TODO: Fix overflow error
     */
    case 0x9: {
        uint16_t negated = (~source_value + 1) & mask;

        result = (dst_value + negated) & mask;

        set_flag(cpu, SR_N, is_negative(result, bw_flag));
        set_flag(cpu, SR_Z, is_zero(result, bw_flag));
        /* the carry may happen during conversion to 2's comp */
        set_flag(cpu, SR_C, source_value == 0 || is_carried(dst_value, negated, bw_flag));
        set_flag(cpu, SR_V, is_overflowed(negated, dst_value, result, bw_flag));
        break;
    }

    /* DADD SOURCE, DESTINATION
     *
     * TODO: not implemented
     */
    case 0xA: {
        break;
    }

    /* BIT SOURCE, DESTINATION
     *
     * N: Set if MSB of result is set, reset otherwise
     * Z: Set if result is zero, reset otherwise
     * C: Set if result is not zero, reset otherwise (.NOT. Zero)
     * V: Reset
     */
    case 0xB: {
        result = source_value & dst_value;

        set_flag(cpu, SR_Z, result == 0);
        set_flag(cpu, SR_N, is_negative(result, bw_flag));
        set_flag(cpu, SR_C, result != 0);
        cpu->sr &= ~SR_V;
        break;
    }

    /* BIC SOURCE, DESTINATION
     *
     * No status bits affected
     */
    case 0xC: {
        // __bic_SR_register_on_exit: a byte op keeps the high byte of SR
        if (dst_is_sr)
            cpu->sr &= ~source_value;
        else
            operand_write(emu, &dst, dst_value & ~source_value, byte);
        break;
    }

    /* BIS SOURCE, DESTINATION
     *
     */
    case 0xD: {
        // __bis_SR_register
        if (dst_is_sr)
            cpu->sr |= source_value;
        else
            operand_write(emu, &dst, dst_value | source_value, byte);
        break;
    }

    /* XOR SOURCE, DESTINATION
     *
     * N: Set if result MSB is set, reset if not set
     * Z: Set if result is zero, reset otherwise
     * C: Set if result is not zero, reset otherwise ( = .NOT. Zero)
     * V: Set if both operands are negative
     */
    case 0xE: {
        result = dst_value ^ source_value;
        operand_write(emu, &dst, result, byte);

        set_flag(cpu, SR_V, is_negative(dst_value, bw_flag) && is_negative(source_value, bw_flag));
        set_flag(cpu, SR_N, is_negative(result, bw_flag));
        set_flag(cpu, SR_Z, result == 0);
        set_flag(cpu, SR_C, result != 0);
        break;
    }

    /* AND SOURCE, DESTINATION
     *
     *  N: Set if result MSB is set, reset if not set
     *  Z: Set if result is zero, reset otherwise
     *  C: Set if result is not zero, reset otherwise ( = .NOT. Zero)
     *  V: Reset
     */
    case 0xF: {
        result = dst_value & source_value;
        operand_write(emu, &dst, result, byte);

        set_flag(cpu, SR_N, is_negative(result, bw_flag));
        set_flag(cpu, SR_Z, result == 0);
        set_flag(cpu, SR_C, result != 0);
        cpu->sr &= ~SR_V;
        break;
    }

    } // # End of switch
}
