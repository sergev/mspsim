#include "emulator.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cpu/registers.h"
#include "debugger/debugger.h"
#include "uart/uart.h"

Emulator *emu_create(void)
{
    Emulator *emu = calloc(1, sizeof(Emulator));
    emu->cpu      = calloc(1, sizeof(Cpu));
    emu->debugger = calloc(1, sizeof(Debugger));
    setup_debugger(emu);

    /* Info memory and the code area read as erased flash. */
    memset(emu->mem + 0x1000, 0xFF, 0x100);
    memset(emu->mem + 0xC000, 0xFF, 0x4000);

    emu_reset(emu);
    emu->cpu->running = false;
    return emu;
}

void emu_reset(Emulator *emu)
{
    cpu_reset(emu);
    uart_reset(emu);
    emu->stopped   = false;
    emu->exit_code = 0;
}

void emu_destroy(Emulator *emu)
{
    if (emu == NULL)
        return;
    free(emu->cpu);
    free(emu->debugger);
    free(emu);
}
