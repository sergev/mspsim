#ifndef _IO_H_
#define _IO_H_

#include <stdint.h>

#include "emulator.h"

/* Hooks implemented by the front-end. */

/* Diagnostic and debugger text. */
void print_console(Emulator *emu, const char *buf);

/* Console UART: send a byte; return the next input byte, or -1 if none is ready. */
void uart_tx(Emulator *emu, uint8_t byte);
int uart_rx(Emulator *emu);

#endif
