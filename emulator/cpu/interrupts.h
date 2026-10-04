#ifndef _INTERRUPTS_H_
#define _INTERRUPTS_H_

#include <stdbool.h>

#include "emulator.h"

/*
 * Vector N lives at 0xFFE0 + 2*N; higher N has higher priority.
 * Vector 14 (NMI) ignores GIE; vector 15 is reset, never requested.
 */
#define VECTOR_TABLE 0xFFE0
#define NMI_IRQ      14
#define RESET_IRQ    15

/* Assert or deassert a level-sensitive request; the source deasserts it. */
void cpu_set_irq(Emulator *emu, unsigned irq, bool level);

/* Accept the highest-priority pending request; true if one was taken. */
bool handle_interrupts(Emulator *emu);

#endif
