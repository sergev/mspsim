#ifndef _IO_H_
#define _IO_H_

#include "emulator.h"

/* Front-end hook; replaced by the I/O interface of Plan steps 5-6. */
void print_console(Emulator *emu, const char *buf);

#endif
