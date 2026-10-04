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
#include "debugger/debugger.h"
#include "io.h"
#include "memory/memory.h"
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
void decode(Emulator *emu, uint16_t instruction, bool disassemble)
{
    Cpu *cpu           = emu->cpu;
    Debugger *debugger = emu->debugger;

    uint8_t FormatId;
    memset(debugger->mnemonic, 0, sizeof debugger->mnemonic);

    FormatId = (uint8_t)(instruction >> 12);

    if (FormatId == 0x1) {
        // format II (single operand) instruction
        decode_formatII(emu, instruction, disassemble);
    } else if (FormatId >= 0x2 && FormatId <= 3) {
        // format III (jump) instruction
        decode_formatIII(emu, instruction, disassemble);
    } else if (FormatId >= 0x4) {
        // format I (two operand) instruction
        decode_formatI(emu, instruction, disassemble);
    } else {
        char inv[100] = { 0 };

        sprintf(inv, "%04X\t[INVALID INSTRUCTION]\n", instruction);
        print_console(emu, inv);
        printf("%s", inv);

        // cpu->pc -= 2;
        cpu->running         = false;
        debugger->debug_mode = true;
    }
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
            printf("Invalid as_flag for CG1\n");
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
            printf("Invalid as_flag for CG2\n");
        }
        }

        break;
    }

    default: {
        printf("Invalid source register for constant generation.\n");
    }
    }

    return generated_constant;
}

void listing_init(Listing *l, uint16_t instruction)
{
    snprintf(l->hex, sizeof l->hex, "%04X", instruction);
    l->ops[0] = 0;
}

void print_listing(Emulator *emu, Listing *l, const char *mnemonic)
{
    char text[128];
    int i;

    if (!emu->debugger->debug_mode)
        return;

    // Make little endian big endian
    for (i = 0; i + 4 <= (int)strlen(l->hex); i += 4) {
        char one = l->hex[i], two = l->hex[i + 1];

        l->hex[i]     = l->hex[i + 2];
        l->hex[i + 1] = l->hex[i + 3];
        l->hex[i + 2] = one;
        l->hex[i + 3] = two;
    }

    printf("%s", l->hex);
    print_console(emu, l->hex);

    for (i = strlen(l->hex); i < 12; i++) {
        printf(" ");
        print_console(emu, " ");
    }

    snprintf(text, sizeof text, "\t%s\t%s\n", mnemonic, l->ops);
    printf("%s", text);
    print_console(emu, text);
}

static uint16_t fetch_ext(Emulator *emu, Listing *l)
{
    char part[8];
    uint16_t word = fetch(emu);

    snprintf(part, sizeof part, "%04X", word);
    str_append(l->hex, sizeof l->hex, part);
    return word;
}

void decode_operand(Emulator *emu, Listing *l, Operand *op, uint8_t reg, uint8_t mode, bool byte,
                    bool is_source, bool disassemble)
{
    Cpu *cpu = emu->cpu;
    char name[10], text[32];

    reg_num_to_name(reg, name);

    if (is_source && ((reg == 2 && mode > 1) || reg == 3)) {
        op->kind  = OPND_CONST;
        op->value = run_constant_generator(reg, mode);
        snprintf(text, sizeof text, "#0x%04X", op->value);
    } else if (mode == 0) {
        op->kind = OPND_REG;
        op->reg  = reg;
        snprintf(text, sizeof text, "%s", name);
    } else if (mode == 1) {
        uint16_t where  = cpu->pc;
        uint16_t offset = fetch_ext(emu, l);

        op->kind = OPND_MEM;
        if (reg == 0) { /* symbolic: relative to the extension word */
            op->addr = where + offset;
            snprintf(text, sizeof text, "0x%04X", op->addr);
        } else if (reg == 2) { /* absolute */
            op->addr = offset;
            snprintf(text, sizeof text, "&0x%04X", offset);
        } else { /* indexed */
            op->addr = cpu->r[reg] + offset;
            snprintf(text, sizeof text, "0x%04X(%s)", offset, name);
        }
    } else if (mode == 2) {
        op->kind = OPND_MEM;
        op->addr = cpu->r[reg];
        snprintf(text, sizeof text, "@%s", name);
    } else if (reg == 0) { /* immediate, @PC+ */
        op->kind  = OPND_CONST;
        op->value = fetch_ext(emu, l);
        snprintf(text, sizeof text, "#0x%04X", byte ? op->value & 0xFF : op->value);
    } else { /* @Rn+; SP always steps by 2 */
        op->kind = OPND_MEM;
        op->addr = cpu->r[reg];
        if (!disassemble)
            cpu->r[reg] += (byte && reg != 1) ? 1 : 2;
        snprintf(text, sizeof text, "@%s+", name);
    }
    str_append(l->ops, sizeof l->ops, text);
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
