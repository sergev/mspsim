#include "uart/uart.h"

#include <stdbool.h>
#include <stdint.h>

#include "cpu/interrupts.h"
#include "cpu/registers.h"
#include "io.h"

static void update_irq(Emulator *emu)
{
    cpu_set_irq(emu, UART_RX_IRQ, (emu->mem[IFG2] & UCA0RXIFG) && (emu->mem[IE2] & UCA0RXIE));
}

/* Move the next input byte, if any, into RXBUF. */
static void poll_input(Emulator *emu)
{
    if ((emu->mem[IFG2] & UCA0RXIFG) || emu->uart_eof)
        return;

    int c = uart_rx(emu);
    if (c == UART_EOF)
        emu->uart_eof = true;
    if (c < 0)
        return;
    emu->mem[UCA0RXBUF] = c;
    emu->mem[IFG2] |= UCA0RXIFG;
    update_irq(emu);
}

void uart_reset(Emulator *emu)
{
    emu->mem[IFG2]    = (emu->mem[IFG2] & ~UCA0RXIFG) | UCA0TXIFG;
    emu->uart_poll_at = 0;
    update_irq(emu);
}

bool uart_may_interrupt(Emulator *emu)
{
    return (emu->mem[IE2] & UCA0RXIE) && !emu->uart_eof;
}

void uart_tick(Emulator *emu)
{
    uint64_t now = emu->cpu->cycles;

    if (now < emu->uart_poll_at)
        return;
    emu->uart_poll_at = now + UART_POLL_CYCLES;
    poll_input(emu);
}

uint8_t uart_read(Emulator *emu, uint16_t addr)
{
    switch (addr) {
    case IFG2:
        poll_input(emu);
        break;
    case UCA0RXBUF:
        if (emu->mem[IFG2] & UCA0RXIFG) {
            emu->mem[IFG2] &= ~UCA0RXIFG;
            update_irq(emu);
        }
        break;
    }
    return emu->mem[addr];
}

void uart_write(Emulator *emu, uint16_t addr, uint8_t val)
{
    switch (addr) {
    case IE2:
        emu->mem[IE2] = val;
        update_irq(emu);
        break;
    case IFG2: /* the UART flags are read-only */
        emu->mem[IFG2] =
            (val & ~(UCA0RXIFG | UCA0TXIFG)) | (emu->mem[IFG2] & UCA0RXIFG) | UCA0TXIFG;
        break;
    case UCA0RXBUF:
        break;
    case UCA0TXBUF:
        emu->mem[UCA0TXBUF] = val;
        uart_tx(emu, val);
        break;
    }
}
