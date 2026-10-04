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

unsigned decode_formatI(Emulator *emu, uint16_t instruction, Listing *l)
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
    operand_increment(emu, &src);

    if (l) {
        snprintf(l->mnemonic, sizeof l->mnemonic, "%s%s", names[opcode - 4], byte ? ".b" : "");
        if (opcode == 0x4 && src.kind == OPND_CONST && dst.kind == OPND_REG && dst.reg == 0)
            listing_target(l, src.value, true); /* br #addr */
        return 0;
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

    /* ADD, ADDC, SUBC, SUB, CMP: DST + SRC + carry-in, with SUBC/SUB/CMP
     * adding ~SRC. CMP only sets the flags.
     *
     * N: Set if result is negative, reset if positive
     * Z: Set if result is zero, reset otherwise
     * C: Set if there is a carry out of the MSB (for subtraction: no borrow)
     * V: Set if an arithmetic overflow occurs, otherwise reset
     */
    case 0x5: /* ADD */
    case 0x6: /* ADDC */
    case 0x7: /* SUBC */
    case 0x8: /* SUB */
    case 0x9: /* CMP */ {
        unsigned carry  = (opcode == 0x5)                    ? 0
                          : (opcode == 0x6 || opcode == 0x7) ? (cpu->sr & SR_C)
                                                             : 1;
        uint16_t addend = (opcode >= 0x7) ? ~source_value & mask : source_value;
        uint32_t sum    = dst_value + addend + carry;

        result = sum & mask;
        if (opcode != 0x9)
            operand_write(emu, &dst, result, byte);

        set_flag(cpu, SR_Z, result == 0);
        set_flag(cpu, SR_N, is_negative(result, bw_flag));
        set_flag(cpu, SR_C, sum > mask);
        set_flag(cpu, SR_V, is_overflowed(addend, dst_value, result, bw_flag));
        break;
    }

    /* DADD SOURCE, DESTINATION
     *
     * DESTINATION = SOURCE + DESTINATION + C, decimally (BCD)
     *
     * N: Set if MSB of result is set
     * Z: Set if result is zero
     * C: Set if the BCD result is greater than 9999 (99 for bytes)
     * V: As for binary addition of the operands and the result
     */
    case 0xA: {
        unsigned carry = cpu->sr & SR_C;

        result = 0;
        for (int shift = 0; shift < (byte ? 8 : 16); shift += 4) {
            unsigned digit = ((source_value >> shift) & 0xF) + ((dst_value >> shift) & 0xF) + carry;

            carry = digit > 9;
            if (carry)
                digit -= 10;
            result |= (digit & 0xF) << shift;
        }
        operand_write(emu, &dst, result, byte);

        set_flag(cpu, SR_Z, result == 0);
        set_flag(cpu, SR_N, is_negative(result, bw_flag));
        set_flag(cpu, SR_C, carry);
        set_flag(cpu, SR_V, is_overflowed(source_value, dst_value, result, bw_flag));
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
            cpu->sr &= ~source_value & SR_MASK;
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
            cpu->sr = (cpu->sr | source_value) & SR_MASK;
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

    /* cycles by source class (Rn, @Rn, @Rn+, #N, x(Rn)/EDE/&EDE) and destination */
    static const uint8_t cycles[5][3] = {
        /* Rm PC  mem */
        { 1, 2, 4 }, { 2, 2, 5 }, { 2, 3, 5 }, { 2, 3, 5 }, { 3, 3, 6 },
    };
    return cycles[cycle_class(source, as_flag)][ad_flag ? 2 : destination == 0 ? 1 : 0];
}
