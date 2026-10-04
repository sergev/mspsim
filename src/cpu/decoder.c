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

#include "cpu/decoder.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cpu/formatI.h"
#include "cpu/formatII.h"
#include "cpu/formatIII.h"
#include "cpu/registers.h"
#include "io.h"
#include "mem/memory.h"
#include "utilities.h"

// ##########+++ CPU Fetch Cycle  +++##########
uint16_t fetch(Emulator *emu)
{
    Cpu *cpu      = emu->cpu;
    uint16_t word = mem_read(emu, cpu->pc, 2, ACC_FETCH);

    cpu->pc += 2;
    return word;
}

// ##########+++ CPU Decode Cycle +++##########
unsigned decode(Emulator *emu, uint16_t instruction, Listing *l)
{
    uint8_t FormatId;

    FormatId = (uint8_t)(instruction >> 12);

    if (FormatId == 0x1) {
        // format II (single operand) instruction
        return decode_formatII(emu, instruction, l);
    } else if (FormatId >= 0x2 && FormatId <= 3) {
        // format III (jump) instruction
        return decode_formatIII(emu, instruction, l);
    } else if (FormatId >= 0x4) {
        // format I (two operand) instruction
        return decode_formatI(emu, instruction, l);
    } else if (l) {
        snprintf(l->mnemonic, sizeof l->mnemonic, ".word");
        snprintf(l->ops, sizeof l->ops, "0x%04x", instruction);
    } else {
        emu->stop = EMU_ILLEGAL;
    }
    return 0;
}

int cycle_class(uint8_t reg, uint8_t mode)
{
    if ((reg == 2 && mode > 1) || reg == 3 || mode == 0)
        return CYC_RN;
    if (mode == 1)
        return CYC_MEM;
    if (mode == 2)
        return CYC_IND;
    return reg == 0 ? CYC_IMM : CYC_INC;
}

// Constant Generator
int16_t run_constant_generator(uint8_t source, uint8_t as_flag)
{
    int16_t generated_constant = 0;

    switch (source) {
    case 2: { /* Register R2/SR/CG1 */
        switch (as_flag) {
        case 0b10: { /* +4, bit processing */
            generated_constant = 4;
            break;
        }
        case 0b11: { /* +8, bit processing */
            generated_constant = 8;
            break;
        }
        default: {
            /* not reached: callers check as_flag */
        }
        }

        break;
    }

    // Register R3/CG2
    case 3: {
        switch (as_flag) {
        case 0b00: { /* 0, word processing */
            generated_constant = 0;
            break;
        }
        case 0b01: { /* +1 */
            generated_constant = 1;
            break;
        }
        case 0b10: { /* +2, bit processing */
            generated_constant = 2;
            break;
        }
        case 0b11: { /* -1, word processing */
            generated_constant = -1;
            break;
        }
        default: {
            /* not reached */
        }
        }

        break;
    }

    default: {
        /* not reached */
    }
    }

    return generated_constant;
}

void listing_init(Listing *l, uint16_t addr, uint16_t instruction)
{
    l->addr        = addr;
    l->words[0]    = instruction;
    l->nwords      = 1;
    l->mnemonic[0] = 0;
    l->ops[0]      = 0;
    l->ntargets    = 0;
}

void listing_target(Listing *l, uint16_t addr, bool code)
{
    if (l->ntargets < 2) {
        l->targets[l->ntargets] = addr;
        l->code[l->ntargets++]  = code;
    }
}

void format_listing(const Listing *l, char *buf, size_t size)
{
    char hex[16] = "";

    for (int i = 0; i < l->nwords; i++) {
        char word[8];

        snprintf(word, sizeof word, i ? " %04x" : "%04x", l->words[i]);
        str_append(hex, sizeof hex, word);
    }
    snprintf(buf, size, "%04x: %-14s   %-5s %s", l->addr, hex, l->mnemonic, l->ops);

    size_t n = strlen(buf);
    while (n > 0 && buf[n - 1] == ' ')
        buf[--n] = 0;
}

static uint16_t fetch_ext(Emulator *emu, Listing *l)
{
    uint16_t word = fetch(emu);

    if (l && l->nwords < 3)
        l->words[l->nwords++] = word;
    return word;
}

void decode_operand(Emulator *emu, Listing *l, Operand *op, uint8_t reg, uint8_t mode, bool byte,
                    bool is_source)
{
    Cpu *cpu       = emu->cpu;
    int16_t offset = 0;

    op->reg = reg;
    op->inc = 0;
    if (is_source && ((reg == 2 && mode > 1) || reg == 3)) {
        op->kind  = OPND_CONST;
        op->value = run_constant_generator(reg, mode);
    } else if (mode == 0) {
        op->kind = OPND_REG;
        op->reg  = reg;
    } else if (mode == 1) {
        uint16_t where = cpu->pc;

        offset   = fetch_ext(emu, l);
        op->kind = OPND_MEM;
        if (reg == 0) /* symbolic: relative to the extension word */
            op->addr = where + offset;
        else if (reg == 2) /* absolute */
            op->addr = offset;
        else /* indexed */
            op->addr = cpu->r[reg] + offset;
    } else if (mode == 2) {
        op->kind = OPND_MEM;
        op->addr = cpu->r[reg];
    } else if (reg == 0) { /* immediate, @PC+ */
        op->kind  = OPND_CONST;
        op->value = fetch_ext(emu, l);
    } else { /* @Rn+; SP always steps by 2 */
        op->kind = OPND_MEM;
        op->addr = cpu->r[reg];
        if (!l)
            op->inc = (byte && reg != 1) ? 1 : 2;
    }
    if (!l)
        return;

    char name[8], text[32];

    reg_num_to_name(reg, name);
    if (op->kind == OPND_CONST)
        snprintf(text, sizeof text, byte ? "#0x%02x" : "#0x%04x",
                 byte ? op->value & 0xFF : op->value);
    else if (mode == 0)
        snprintf(text, sizeof text, "%s", name);
    else if (mode == 1 && reg == 0) {
        snprintf(text, sizeof text, "0x%04x", op->addr);
        listing_target(l, op->addr, false);
    } else if (mode == 1 && reg == 2) {
        snprintf(text, sizeof text, "&0x%04x", op->addr);
        listing_target(l, op->addr, false);
    } else if (mode == 1)
        snprintf(text, sizeof text, "%d(%s)", offset, name);
    else
        snprintf(text, sizeof text, mode == 2 ? "@%s" : "@%s+", name);
    str_append(l->ops, sizeof l->ops, text);
}

void operand_increment(Emulator *emu, Operand *op)
{
    if (op->inc) {
        emu->cpu->r[op->reg] += op->inc;
        op->inc = 0;
    }
}

uint16_t operand_read(Emulator *emu, const Operand *op, bool byte)
{
    uint16_t val;

    switch (op->kind) {
    case OPND_REG:
        val = emu->cpu->r[op->reg];
        break;
    case OPND_MEM:
        return mem_read(emu, op->addr, byte ? 1 : 2, ACC_DATA);
    default:
        val = op->value;
        break;
    }
    return byte ? (val & 0xFF) : val;
}

void operand_write(Emulator *emu, const Operand *op, uint16_t val, bool byte)
{
    switch (op->kind) {
    case OPND_REG:
        reg_write(emu->cpu, op->reg, val, byte);
        break;
    case OPND_MEM:
        mem_write(emu, op->addr, val, byte ? 1 : 2);
        break;
    default: /* writes to constants are lost */
        break;
    }
}
