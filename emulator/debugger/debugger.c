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

#include "debugger/debugger.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cpu/registers.h"
#include "debugger/disassembler.h"
#include "debugger/register_display.h"
#include "io.h"
#include "loader/symbols.h"
#include "memory/memory.h"
#include "utilities.h"

static bool is_cmd(const char *cmd, const char *name)
{
    return strcasecmp(cmd, name) == 0;
}

/* A symbol name or a hex address. */
static bool parse_addr(Emulator *emu, const char *s, uint16_t *addr)
{
    char *end;
    unsigned long v;

    if (symbol_lookup(emu, s, addr))
        return true;
    v = strtoul(s, &end, 16);
    if (end == s || *end != 0 || v > 0xFFFF)
        return false;
    *addr = v;
    return true;
}

/* Registers and the next instruction. */
static void show_state(Emulator *emu)
{
    display_registers(emu);
    disassemble(emu, emu->cpu->pc, 1);
}

void report_stop(Emulator *emu)
{
    Cpu *cpu = emu->cpu;

    switch (emu->stop) {
    case EMU_RUNNING:
        return;
    case EMU_PROGRAM:
        emu_printf(emu, "\n\t[Stopped by program, exit code %u]\n", emu->exit_code);
        break;
    case EMU_BREAKPOINT:
        emu_printf(emu, "\n\t[Breakpoint %d hit]\n", breakpoint_at(emu, cpu->pc) + 1);
        break;
    default:
        emu_printf(emu, "\n\t[%s]\n", emu_stop_reason(emu->stop));
        break;
    }
    show_state(emu);
}

DebugAction exec_cmd(Emulator *emu, const char *line)
{
    Cpu *cpu      = emu->cpu;
    Debugger *deb = emu->debugger;

    char cmd[100] = { 0 }, arg[100] = { 0 };
    unsigned int op1 = 0;
    int ops;

    ops = sscanf(line, "%99s %99s", cmd, arg);
    if (ops < 1)
        return DBG_STAY;

    /* RESET / RESTART

       Resets the entire virtual machine to it's starting state.
       Puts the starting address back into Program Counter
     */
    if (is_cmd(cmd, "reset") || is_cmd(cmd, "restart")) {
        emu_reset(emu);
        show_state(emu);
    }

    // s [NUM], step NUM instructions forward, defaults to 1 //
    else if (is_cmd(cmd, "s") || is_cmd(cmd, "step")) {
        uint32_t steps = 1; // 1 step by default

        if (ops == 2 && sscanf(arg, "%u", &op1) == 1)
            steps = op1;

        emu->stop = EMU_RUNNING;
        for (uint32_t i = 0; i < steps && emu->stop == EMU_RUNNING; i++)
            cpu_step(emu);
        if (emu->stop != EMU_RUNNING)
            report_stop(emu);
        else
            disassemble(emu, cpu->pc, 1);
    }

    // Quit program //
    else if (is_cmd(cmd, "quit") || is_cmd(cmd, "q")) {
        return DBG_QUIT;
    }

    // run the program until a breakpoint is hit //
    else if (is_cmd(cmd, "run") || is_cmd(cmd, "r") || is_cmd(cmd, "c") ||
             is_cmd(cmd, "continue")) {
        return DBG_RUN;
    }

    // Display disassembly of N at HEX_ADDR: dis [N] [HEX_ADDR] //
    else if (is_cmd(cmd, "disas") || is_cmd(cmd, "dis") || is_cmd(cmd, "disassemble")) {
        unsigned int num    = 10;
        uint16_t start_addr = cpu->pc;
        char where[100]     = "";

        sscanf(line, "%*s %u %99s", &num, where);
        if (where[0] && !parse_addr(emu, where, &start_addr)) {
            emu_printf(emu, "\t[No symbol or address %s]\n", where);
            return DBG_STAY;
        }
        disassemble(emu, start_addr, num);
    }

    // dump [HEX_ADDR|Rn] //
    else if (is_cmd(cmd, "dump")) {
        uint16_t start_addr = cpu->pc;
        int reg             = reg_name_to_num(arg);

        // A register holding the address, a symbol or a hex address
        if (reg >= 0) {
            start_addr = cpu->r[reg];
        } else if (arg[0] && !parse_addr(emu, arg, &start_addr)) {
            emu_printf(emu, "\t[No register, symbol or address %s]\n", arg);
            return DBG_STAY;
        }
        dump_memory(emu, start_addr, BYTE_STRIDE);
    }

    // set [HEX_ADDR|Rn] HEX_VALUE //
    else if (is_cmd(cmd, "set")) {
        unsigned int value = 0;
        uint16_t addr;

        if (sscanf(line, "%*s %*s %X", &value) != 1) {
            emu_printf(emu, "\t[Usage: set ADDR|Rn HEX_VALUE]\n");
            return DBG_STAY;
        }

        // Figure out if the value given is a reg name or addr
        int res = reg_name_to_num(arg);

        if (res != -1) { // If its a reg name
            cpu->r[res] = value;
            show_state(emu);
        } else if (parse_addr(emu, arg, &addr)) {
            mem_write(emu, addr, value, 2);
        } else {
            emu_printf(emu, "\t[No register, symbol or address %s]\n", arg);
        }
    }

    // break BREAKPOINT_ADDRESS - set breakpoint //
    else if (is_cmd(cmd, "break") || is_cmd(cmd, "b")) {
        uint16_t addr;

        if (deb->num_bps >= MAX_BREAKPOINTS) {
            emu_printf(emu, "Breakpoints are full.\n");
        } else if (ops == 2 && parse_addr(emu, arg, &addr)) {
            deb->bp_addresses[deb->num_bps++] = addr;
            emu_printf(emu, "\t[Breakpoint [%d] Set at 0x%04X]\n", deb->num_bps, addr);
        } else {
            emu_printf(emu, "\t[Usage: break NAME|HEX_ADDR]\n");
        }
    }

    // Display all breakpoints //
    else if (is_cmd(cmd, "bps")) {
        if (deb->num_bps == 0)
            emu_printf(emu, "You have not set any breakpoints!\n");
        for (uint32_t i = 0; i < deb->num_bps; i++) {
            const Symbol *s = symbol_at(emu, deb->bp_addresses[i]);

            emu_printf(emu, "\t[%d] 0x%04X%s%s\n", i + 1, deb->bp_addresses[i], s ? " " : "",
                       s ? s->name : "");
        }
    }

    // Display registers //
    else if (is_cmd(cmd, "regs")) {
        show_state(emu);
    }

    // trace on|off //
    else if (is_cmd(cmd, "trace")) {
        if (is_cmd(arg, "on"))
            emu->trace = true;
        else if (is_cmd(arg, "off"))
            emu->trace = false;
        else if (arg[0])
            emu_printf(emu, "\t[Usage: trace on|off]\n");
        emu_printf(emu, "\t[Tracing is %s]\n", emu->trace ? "on" : "off");
    }

    // help, display a list of debugger cmds //
    else if (is_cmd(cmd, "help") || is_cmd(cmd, "h")) {
        display_help(emu);
    }

    else {
        emu_printf(emu, "\t[Invalid command, type \"help\".]\n");
    }

    return DBG_STAY;
}

// ##########+++ Dump Memory Function +++##########
void dump_memory(Emulator *emu, uint16_t start_addr, uint8_t stride)
{
    uint16_t msp_addr = start_addr;
    uint8_t MEM[8];

    emu_printf(emu, "\n");

    for (int i = 0; i < 32; i += 8) {
        for (int k = 0; k < 8; k++)
            MEM[k] = emu->mem[(uint16_t)(msp_addr + k)];

        emu_printf(emu, "0x%04X:\t", msp_addr);

        if (stride == BYTE_STRIDE) {
            emu_printf(emu,
                       "0x%02X  0x%02X  0x%02X  0x%02X  "
                       "0x%02X  0x%02X  0x%02X  0x%02X\n",
                       MEM[0], MEM[1], MEM[2], MEM[3], MEM[4], MEM[5], MEM[6], MEM[7]);
        } else if (stride == WORD_STRIDE) {
            emu_printf(emu, "0x%02X%02X  0x%02X%02X  0x%02X%02X  0x%02X%02X\n", MEM[0], MEM[1],
                       MEM[2], MEM[3], MEM[4], MEM[5], MEM[6], MEM[7]);
        } else if (stride == DWORD_STRIDE) {
            emu_printf(emu, "0x%02X%02X%02X%02X  0x%02X%02X%02X%02X\n", MEM[0], MEM[1], MEM[2],
                       MEM[3], MEM[4], MEM[5], MEM[6], MEM[7]);
        }

        msp_addr += 8;
    }

    emu_printf(emu, "\n");
}

void setup_debugger(Emulator *emu)
{
    Debugger *deb = emu->debugger;

    deb->color = false;
    memset(deb->bp_addresses, 0, sizeof(deb->bp_addresses));
    deb->num_bps = 0;
}

int breakpoint_at(Emulator *emu, uint16_t addr)
{
    Debugger *deb = emu->debugger;

    for (uint32_t i = 0; i < deb->num_bps; i++)
        if (deb->bp_addresses[i] == addr)
            return i;
    return -1;
}
