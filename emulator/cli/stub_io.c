#include <stdio.h>

#include "emulator.h"
#include "io.h"

void print_console(Emulator *emu, const char *buf)
{
    (void)emu;
    fputs(buf, stderr);
}
