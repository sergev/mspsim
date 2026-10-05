#ifndef _UART_H_
#define _UART_H_

#include <stdbool.h>
#include <stdint.h>

#include "emulator.h"

/*
 * Console UART: a subset of USCI_A0 at the MSP430G2xx addresses.
 * Its registers live in emu->mem, so the debugger sees their state.
 */
enum {
    IE2       = 0x0001,
    IFG2      = 0x0003,
    UCA0RXBUF = 0x0066,
    UCA0TXBUF = 0x0067,
};

enum {
    UCA0RXIE  = 0x01, /* IE2 */
    UCA0RXIFG = 0x01, /* IFG2: input byte available */
    UCA0TXIFG = 0x02, /* IFG2: always set */
};

#define UART_RX_IRQ 7 /* vector 0xFFEE */

/* Cycles between console input polls. */
#define UART_POLL_CYCLES 4096

void uart_reset(Emulator *emu);

/* Called every CPU step; polls for input every UART_POLL_CYCLES. */
void uart_tick(Emulator *emu);

/* The RX interrupt is enabled and more input may come. */
bool uart_may_interrupt(Emulator *emu);

/* Take the input byte waiting in RXBUF, if any, as a read of RXBUF would;
 * -1 if there is none. Host reads of stdin (hostio) come after it. */
int uart_take_input(Emulator *emu);

uint8_t uart_read(Emulator *emu, uint16_t addr);
void uart_write(Emulator *emu, uint16_t addr, uint8_t val);

#endif
