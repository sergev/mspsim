/*
 * Temporary CLI driver: mspsim firmware.bin [max_steps]
 * Replaced by the real front-end in Step 6.
 */
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "cpu/registers.h"
#include "debugger/debugger.h"
#include "debugger/register_display.h"
#include "emulator.h"
#include "utilities.h"

static Emulator *sigint_emu;

static void handle_sigint(int sig)
{
    (void)sig;
    if (sigint_emu == NULL)
        return;
    sigint_emu->cpu->running         = false;
    sigint_emu->debugger->debug_mode = true;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s firmware.bin [max_steps]\n", argv[0]);
        return 2;
    }
    unsigned long long max_steps = (argc > 2) ? strtoull(argv[2], NULL, 0) : 0;

    Emulator *emu = emu_create();
    Cpu *cpu      = emu->cpu;
    Debugger *deb = emu->debugger;

    sigint_emu = emu;
    signal(SIGINT, handle_sigint);

    int status = 1;
    if (load_firmware(emu, argv[1], 0xC000) == 0) {
        cpu->running    = true;
        deb->debug_mode = false;
        for (unsigned long long n = 0;
             cpu->running && !deb->quit && (max_steps == 0 || n < max_steps); n++)
            cpu_step(emu);
        display_registers(emu);
        status = 0;
    }

    sigint_emu = NULL;
    emu_destroy(emu);
    return status;
}
