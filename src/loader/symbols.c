#include "loader/symbols.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void symbols_add(Emulator *emu, const char *name, uint16_t addr, int rank)
{
    if (emu->nsymbols % 64 == 0)
        emu->symbols = realloc(emu->symbols, (emu->nsymbols + 64) * sizeof(Symbol));
    emu->symbols[emu->nsymbols++] = (Symbol){ .name = strdup(name), .addr = addr, .rank = rank };
}

static int by_addr(const void *a, const void *b)
{
    const Symbol *x = a, *y = b;

    if (x->addr != y->addr)
        return x->addr < y->addr ? -1 : 1;
    if (x->rank != y->rank)
        return x->rank < y->rank ? -1 : 1;
    return strcmp(x->name, y->name);
}

void symbols_sort(Emulator *emu)
{
    if (emu->nsymbols > 0)
        qsort(emu->symbols, emu->nsymbols, sizeof(Symbol), by_addr);
}

void symbols_free(Emulator *emu)
{
    for (int i = 0; i < emu->nsymbols; i++)
        free(emu->symbols[i].name);
    free(emu->symbols);
    emu->symbols  = NULL;
    emu->nsymbols = 0;
}

bool symbol_lookup(Emulator *emu, const char *name, uint16_t *addr)
{
    for (int i = 0; i < emu->nsymbols; i++) {
        if (strcmp(emu->symbols[i].name, name) == 0) {
            *addr = emu->symbols[i].addr;
            return true;
        }
    }
    return false;
}

/* Index of the first symbol with address > addr. */
static int upper_bound(Emulator *emu, uint16_t addr)
{
    int lo = 0, hi = emu->nsymbols;

    while (lo < hi) {
        int mid = (lo + hi) / 2;

        if (emu->symbols[mid].addr <= addr)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

const Symbol *symbol_before(Emulator *emu, uint16_t addr)
{
    int i = upper_bound(emu, addr);

    if (i == 0)
        return NULL;

    /* first, i.e. preferred, of the symbols sharing that address */
    uint16_t at = emu->symbols[i - 1].addr;
    while (i > 1 && emu->symbols[i - 2].addr == at)
        i--;
    return &emu->symbols[i - 1];
}

const Symbol *symbol_at(Emulator *emu, uint16_t addr)
{
    const Symbol *s = symbol_before(emu, addr);

    return (s && s->addr == addr) ? s : NULL;
}
