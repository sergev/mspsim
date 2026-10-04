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

#ifndef _DECODER_H_
#define _DECODER_H_

#include <stdbool.h>
#include <stdint.h>

#include "emulator.h"

int16_t run_constant_generator(uint8_t source, uint8_t as_flag);

void decode(Emulator *emu, uint16_t instruction, bool disassemble);

/* Next instruction word at PC (ACC_FETCH); PC += 2. */
uint16_t fetch(Emulator *emu);

enum { EMU_WORD, EMU_BYTE };

enum {
    EXECUTE = 0,
    DISASSEMBLE,
};

/* An operand, resolved once its extension word is fetched. */
typedef enum { OPND_REG, OPND_MEM, OPND_CONST } OperandKind;

typedef struct {
    OperandKind kind;
    uint8_t reg;    /* OPND_REG */
    uint16_t addr;  /* OPND_MEM */
    uint16_t value; /* OPND_CONST: immediate or constant generator */
} Operand;

/* Disassembly text of the instruction being decoded. */
typedef struct {
    char hex[32]; /* instruction and extension words */
    char ops[64]; /* operand list */
} Listing;

void listing_init(Listing *l, uint16_t instruction);

/* Print the listing, in debug mode only. */
void print_listing(Emulator *emu, Listing *l, const char *mnemonic);

/*
 * Decode a source (As, constant generators apply) or destination (Ad)
 * operand: fetch its extension word, apply @Rn+ (not when disassembling),
 * and append its text to the listing.
 */
void decode_operand(Emulator *emu, Listing *l, Operand *op, uint8_t reg, uint8_t mode, bool byte,
                    bool is_source, bool disassemble);

uint16_t operand_read(Emulator *emu, const Operand *op, bool byte);
void operand_write(Emulator *emu, const Operand *op, uint16_t val, bool byte);

#endif
