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

#include "cpu/flag_handler.h"

#include <stdbool.h>
#include <stdint.h>

#include "cpu/decoder.h"

/**
 * @brief Test if the result of the asm instruction is zero
 * @param result The operation's result
 * @param bw_flag Byte or Word flag
 * @return true if zero, false otherwise
 */
uint8_t is_zero(uint16_t result, uint8_t bw_flag)
{
    if (bw_flag == EMU_BYTE)
        result &= 0xFF;
    return result == 0;
}

/**
 * @brief Test if the result of the asm instruction is negative
 * @param result The operation's result
 * @param bw_flag Byte or Word flag
 * @return true if negative, false otherwise
 */
uint8_t is_negative(uint16_t result, uint8_t bw_flag)
{
    return (bw_flag == EMU_BYTE) ? (result >> 7) & 1 : result >> 15;
}

/**
 * @brief Test if the result of the asm instruction WILL carry
 * @param original_dst_value The original value at the destination
 * @param source_value The value at the source location
 * @param bw_flag Byte or Word flag
 * @return true if zero, false otherwise
 */
uint8_t is_carried(uint32_t original_dst_value, uint32_t source_value, uint8_t bw_flag)
{
    if (bw_flag == EMU_WORD) {
        if ((65535 - (uint16_t)source_value) < (uint16_t)original_dst_value ||
            ((original_dst_value + source_value) >> 16) != 0) {
            return 1;
        }

        return 0;
    } else if (bw_flag == EMU_BYTE) {
        if ((255 - (uint8_t)source_value) < (uint8_t)original_dst_value ||
            ((original_dst_value + source_value) >> 8) != 0) {
            return 1;
        }

        return 0;
    }

    return false;
}

/**
 * @brief Test if the result of the asm instruction is overflowed
 * @param source_value The value at the source operand
 * @param destination_value The value at the destination operand
 * @param result The result of the operation
 * @param bw_flag Byte or Word flag
 * @return true if overflowed, false otherwise
 */
uint8_t is_overflowed(uint16_t source_value, uint16_t destination_value, uint16_t result,
                      uint8_t bw_flag)
{
    unsigned sign = (bw_flag == EMU_BYTE) ? 7 : 15;
    unsigned s = (source_value >> sign) & 1, d = (destination_value >> sign) & 1;

    return s == d && ((result >> sign) & 1) != d;
}
