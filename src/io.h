#ifndef _IO_H_
#define _IO_H_

#include <stdint.h>

#include "emulator.h"

/* Hooks implemented by the front-end. */

/* Diagnostic and debugger text. */
void print_console(Emulator *emu, const char *buf);

/* Console UART: send a byte; return the next input byte, -1 if none is ready yet,
 * or UART_EOF once input is exhausted. */
#define UART_EOF (-2)
void uart_tx(Emulator *emu, uint8_t byte);
int uart_rx(Emulator *emu);

/* Program I/O through newlib's host calls (hostio/hostio.h): write n bytes to
 * stdout (fd 1) or stderr (fd 2); read up to n > 0 bytes of stdin, waiting for
 * at least one. Each returns the count, 0 at end of input, or -1. */
int host_write(Emulator *emu, int fd, const uint8_t *buf, int n);
int host_read(Emulator *emu, uint8_t *buf, int n);

/* Formatted text through print_console(). */
void emu_printf(Emulator *emu, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif
