#ifndef _SYMBOLS_H_
#define _SYMBOLS_H_

#include <stdbool.h>
#include <stdint.h>

#include "emulator.h"

/* Symbol table from the firmware, sorted by address. */

typedef struct Symbol {
    char *name;
    uint16_t addr;
    uint8_t rank; /* among symbols at one address, the lowest rank is preferred */
} Symbol;

enum { SYM_FUNC = 0, SYM_LABEL = 1, SYM_DATA = 2 }; /* ranks */

void symbols_add(Emulator *emu, const char *name, uint16_t addr, int rank);
void symbols_sort(Emulator *emu);
void symbols_free(Emulator *emu);

/* Address of a symbol by name. */
bool symbol_lookup(Emulator *emu, const char *name, uint16_t *addr);

/* Preferred symbol at exactly addr, or NULL. */
const Symbol *symbol_at(Emulator *emu, uint16_t addr);

/* Preferred symbol at the highest address <= addr, or NULL. */
const Symbol *symbol_before(Emulator *emu, uint16_t addr);

#endif
