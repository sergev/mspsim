#include "io.h"

#include <stdarg.h>
#include <stdio.h>

void emu_printf(Emulator *emu, const char *fmt, ...)
{
    char buf[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    print_console(emu, buf);
}
