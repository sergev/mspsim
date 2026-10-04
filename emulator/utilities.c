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

#include "utilities.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "io.h"

/**
 * @brief Load a raw binary image into emulated memory at addr
 * @return The number of bytes loaded, or -1 with errno set
 */
long load_binary(Emulator *emu, const char *file_name, uint16_t addr)
{
    FILE *fd = fopen(file_name, "rb");
    long size;

    if (fd == NULL)
        return -1;

    /* obtain file size */
    if (fseek(fd, 0, SEEK_END) < 0 || (size = ftell(fd)) < 0) {
        fclose(fd);
        return -1;
    }
    rewind(fd);

    if (size > 0x10000L - addr) {
        fclose(fd);
        errno = EFBIG;
        return -1;
    }
    if (fread(emu->mem + addr, 1, size, fd) != (size_t)size) {
        fclose(fd);
        errno = EIO;
        return -1;
    }
    fclose(fd);
    return size;
}

/**
 * @brief Convert register ASCII name to it's respective numeric value
 * @param name The register's ASCII name
 * @return The numeric equivalent for the register on success, -1 if an
 * invalid name was supplied
 */
int8_t reg_name_to_num(char *name)
{
    if (!strncasecmp("%r0", name, sizeof "%r0") || !strncasecmp("r0", name, sizeof "r0") ||
        !strncasecmp("%pc", name, sizeof "%pc") || !strncasecmp("pc", name, sizeof "pc")) {
        return 0;
    } else if (!strncasecmp("%r1", name, sizeof "%r1") || !strncasecmp("r1", name, sizeof "r1") ||
               !strncasecmp("%sp", name, sizeof "%sp") || !strncasecmp("sp", name, sizeof "sp")) {
        return 1;
    } else if (!strncasecmp("%r2", name, sizeof "%r2") || !strncasecmp("r2", name, sizeof "r2") ||
               !strncasecmp("%sr", name, sizeof "%sr") || !strncasecmp("sr", name, sizeof "sr")) {
        return 2;
    } else if (!strncasecmp("%r3", name, sizeof "%r3") || !strncasecmp("r3", name, sizeof "r3") ||
               !strncasecmp("%cg2", name, sizeof "%cg2") ||
               !strncasecmp("cg2", name, sizeof "cg2")) {
        return 3;
    } else if (!strncasecmp("%r4", name, sizeof "%r4") || !strncasecmp("r4", name, sizeof "r4")) {
        return 4;
    } else if (!strncasecmp("%r5", name, sizeof "%r5") || !strncasecmp("r5", name, sizeof "r5")) {
        return 5;
    } else if (!strncasecmp("%r6", name, sizeof "%r6") || !strncasecmp("r6", name, sizeof "r6")) {
        return 6;
    } else if (!strncasecmp("%r7", name, sizeof "%r7") || !strncasecmp("r7", name, sizeof "r7")) {
        return 7;
    } else if (!strncasecmp("%r8", name, sizeof "%r8") || !strncasecmp("r8", name, sizeof "r8")) {
        return 8;
    } else if (!strncasecmp("%r9", name, sizeof "%r9") || !strncasecmp("r9", name, sizeof "r9")) {
        return 9;
    } else if (!strncasecmp("%r10", name, sizeof "%r10") ||
               !strncasecmp("r10", name, sizeof "r10")) {
        return 10;
    } else if (!strncasecmp("%r11", name, sizeof "%r11") ||
               !strncasecmp("r11", name, sizeof "r11")) {
        return 11;
    } else if (!strncasecmp("%r12", name, sizeof "%r12") ||
               !strncasecmp("r12", name, sizeof "r12")) {
        return 12;
    } else if (!strncasecmp("%r13", name, sizeof "%r13") ||
               !strncasecmp("r13", name, sizeof "r13")) {
        return 13;
    } else if (!strncasecmp("%r14", name, sizeof "%r14") ||
               !strncasecmp("r14", name, sizeof "r14")) {
        return 14;
    } else if (!strncasecmp("%r15", name, sizeof "%r15") ||
               !strncasecmp("r15", name, sizeof "r15")) {
        return 15;
    }

    return -1;
}

/**
 * @brief Convert register number into its ASCII name
 * @param number The register number (0, 1, 2, ...) associated with a
 * register's name like (R0, R1, R2, ...)
 * @param name A pointer to an allocated character array to fill up with
 * the register's ASCII name
 */
void reg_num_to_name(uint8_t number, char *name)
{
    static const char *const names[16] = { "pc", "sp", "sr",  "r3",  "r4",  "r5",  "r6",  "r7",
                                           "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };

    strcpy(name, number < 16 ? names[number] : "???");
}

/**
 * @brief This function displays the help menu to the user if he (or - but in
 * practice all too seldom - she) enters incorrect parameters or prompts the
 * help menu with "help" or "h"
 */
static const char *HelpStr =
    "**************************************************\n"
    "*\t\tmspsim debugger\n*\n"
    "* run, c\t\t[Run Program Until Breakpoint is Hit]\n"
    "* step [N]\t\t[Step Into Instruction]\n"
    "* dump [HEX_ADDR|Rn]\t[Dump Memory direct or at register value]\n"
    "* set HEX_ADDR|Rn VAL\t[Set Memory Word or Register]\n"
    "* dis [N][HEX_ADDR]\t[Disassemble Instructions]\n"
    "* break ADDR\t\t[Set a Breakpoint]\n"
    "* bps\t\t\t[Display Breakpoints]\n"
    "* regs\t\t\t[Display Registers]\n"
    "* trace on|off\t\t[Trace Executed Instructions]\n"
    "* CTRL+C, CTRL+]\t[Pause Execution]\n"
    "* reset\t\t\t[Reset Machine]\n"
    "* quit\t\t\t[Exit program]\n"
    "**************************************************\n";

void display_help(Emulator *emu)
{
    emu_printf(emu, "%s", HelpStr);
}
