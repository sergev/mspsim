/*
 * Small MSP430 assembler for tests. It accepts the syntax of the
 * openMSP430 and binutils test sources: labels (including 1: / 1f / 1b),
 * .set/.equ, .word/.byte, .text/.data/.section .vectors, all core and
 * emulated instructions, and expressions with + - * / ( ) and $.
 * Immediates 0, 1, 2, 4, 8 and -1 use the constant generators, as gas does.
 *
 * Sections: .text at ASM_TEXT, .data at ASM_DATA (also __data_start),
 * .vectors at 0xFFE0. The binutils macros start/pass/fail are built in:
 * pass and fail write 0 and 1 to the stop register.
 */
#ifndef _ASM_H_
#define _ASM_H_

#include <stddef.h>

#include "emulator.h"

#define ASM_TEXT 0xC000
#define ASM_DATA 0x0200

/* Assemble into emu->mem; returns 0, or -1 with "line N: message" in err. */
int asm_text(Emulator *emu, const char *text, char *err, size_t errsize);
int asm_file(Emulator *emu, const char *path, char *err, size_t errsize);

#endif
