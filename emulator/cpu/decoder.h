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
#include <stddef.h>
#include <stdint.h>

#include "emulator.h"

int16_t run_constant_generator(uint8_t source, uint8_t as_flag);

/* Disassembly of one instruction. */
typedef struct {
    uint16_t addr;
    uint16_t words[3]; /* instruction and extension words */
    int nwords;
    char mnemonic[8];
    char ops[48]; /* operand list */

    /* addresses the instruction refers to, for symbol annotation */
    int ntargets;
    uint16_t targets[2];
    bool code[2]; /* jump or call target, rather than data */
} Listing;

void listing_init(Listing *l, uint16_t addr, uint16_t instruction);
void listing_target(Listing *l, uint16_t addr, bool code);

/* One line, no newline: "c004: 40b2 5a80 0120   mov   #0x5a80, &0x0120". */
void format_listing(const Listing *l, char *buf, size_t size);

/*
 * Execute an instruction whose opcode word was just fetched (l == NULL),
 * or disassemble it into l, prepared by listing_init(): no data accesses,
 * no register changes except PC.
 */
void decode(Emulator *emu, uint16_t instruction, Listing *l);

/* Next instruction word at PC (ACC_FETCH); PC += 2. */
uint16_t fetch(Emulator *emu);

enum { EMU_WORD, EMU_BYTE };

/* An operand, resolved once its extension word is fetched. */
typedef enum { OPND_REG, OPND_MEM, OPND_CONST } OperandKind;

typedef struct {
    OperandKind kind;
    uint8_t reg;    /* OPND_REG */
    uint16_t addr;  /* OPND_MEM */
    uint16_t value; /* OPND_CONST: immediate or constant generator */
} Operand;

/*
 * Decode a source (As, constant generators apply) or destination (Ad)
 * operand and fetch its extension word. When disassembling (l != NULL),
 * append its text to l->ops instead of applying @Rn+.
 */
void decode_operand(Emulator *emu, Listing *l, Operand *op, uint8_t reg, uint8_t mode, bool byte,
                    bool is_source);

uint16_t operand_read(Emulator *emu, const Operand *op, bool byte);
void operand_write(Emulator *emu, const Operand *op, uint16_t val, bool byte);

#endif
