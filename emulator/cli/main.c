/*
 * Temporary CLI driver: msp430-sim firmware.bin [max_steps]
 * Replaced by the real front-end in Step 6.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "cpu/registers.h"
#include "debugger/debugger.h"
#include "debugger/register_display.h"
#include "emulator.h"
#include "memory/memspace.h"
#include "utilities.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s firmware.bin [max_steps]\n", argv[0]);
        return 2;
    }
    unsigned long long max_steps = (argc > 2) ? strtoull(argv[2], NULL, 0) : 0;

    Emulator *emu = calloc(1, sizeof(Emulator));
    emu->cpu      = calloc(1, sizeof(Cpu));
    emu->debugger = calloc(1, sizeof(Debugger));
    setup_debugger(emu);

    Cpu *cpu      = emu->cpu;
    Debugger *deb = emu->debugger;

    initialize_msp_memspace();
    initialize_msp_registers(emu);

    int status = 1;
    if (load_firmware(emu, argv[1], 0xC000) == 0) {
        cpu->running    = true;
        deb->debug_mode = false;
        for (unsigned long long n = 0; !deb->quit && (max_steps == 0 || n < max_steps); n++)
            cpu_step(emu);
        display_registers(emu);
        status = 0;
    }

    uninitialize_msp_memspace();
    free(cpu);
    free(deb);
    free(emu);
    return status;
}
