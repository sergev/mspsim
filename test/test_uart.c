#include <string.h>

#include "harness.h"
#include "mem/memory.h"
#include "uart/uart.h"

static uint8_t read_byte(Emulator *emu, uint16_t addr)
{
    return mem_read(emu, addr, 1, ACC_DATA);
}

TEST(tx_sends_byte)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x40F2, 'H', UCA0TXBUF, /* mov.b #'H', &UCA0TXBUF */
            0x40F2, 'i', UCA0TXBUF);        /* mov.b #'i', &UCA0TXBUF */
    step(emu, 2);
    CHECK(strcmp(uart_output, "Hi") == 0);
}

TEST(txifg_always_set)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x4255, IFG2); /* mov.b &IFG2, r5 */
    step(emu, 1);
    CHECK_EQ(emu->cpu->r5, UCA0TXIFG);
}

TEST(ifg2_flags_read_only)
{
    Emulator *emu = emu_new();

    mem_write(emu, IFG2, 0x0C, 1);
    CHECK_EQ(read_byte(emu, IFG2), 0x0C | UCA0TXIFG);
}

TEST(rx_one_byte_at_a_time)
{
    Emulator *emu = emu_new();

    uart_input = "ab";
    CHECK_EQ(read_byte(emu, IFG2), UCA0TXIFG | UCA0RXIFG);
    CHECK_EQ(read_byte(emu, IFG2), UCA0TXIFG | UCA0RXIFG);
    CHECK_EQ(read_byte(emu, UCA0RXBUF), 'a');
    CHECK_EQ(read_byte(emu, IFG2), UCA0TXIFG | UCA0RXIFG);
    CHECK_EQ(read_byte(emu, UCA0RXBUF), 'b');
    CHECK_EQ(read_byte(emu, IFG2), UCA0TXIFG);
}

TEST(rx_interrupt)
{
    Emulator *emu = emu_new();
    Cpu *cpu      = emu->cpu;

    PROGRAM(0xC000, 0xD3D2, IE2,       /* bis.b #UCA0RXIE, &IE2 */
            0xD232,                    /* eint */
            0x3FFF);                   /* jmp $ */
    PROGRAM(0xC010, 0x4254, UCA0RXBUF, /* mov.b &UCA0RXBUF, r4 */
            0x1300);                   /* reti */
    poke(VECTOR_TABLE + 2 * UART_RX_IRQ, 0xC010);
    uart_input = "x";

    step(emu, 2); /* bis, eint, then accept */
    CHECK_EQ(cpu->pc, 0xC010);
    step(emu, 2); /* mov, reti */
    CHECK_EQ(cpu->r4, 'x');
    CHECK_EQ(cpu->pc, 0xC006);
    CHECK(!(cpu->irq_pending & (1u << UART_RX_IRQ)));
}

TEST(rx_interrupt_needs_rxie)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0xD232, /* eint */
            0x3FFF);        /* jmp $ */
    uart_input = "x";
    step(emu, 3);
    CHECK_EQ(emu->cpu->irq_pending, 0);
}

TEST(other_usci_registers_are_memory)
{
    Emulator *emu = emu_new();

    mem_write(emu, 0x0061, 0x81, 1); /* UCA0CTL1 */
    CHECK_EQ(read_byte(emu, 0x0061), 0x81);
}

TEST(stop_word)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x40B2, 0x012A, 0x01FE); /* mov #0x12a, &0x01fe */
    step(emu, 1);
    CHECK_EQ(emu->stop, EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 0x012A);
}

TEST(stop_byte)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x40F2, 0x0007, 0x01FE); /* mov.b #7, &0x01fe */
    step(emu, 1);
    CHECK_EQ(emu->stop, EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 7);
}

TEST(reset_clears_stop)
{
    Emulator *emu = emu_new();

    mem_write(emu, 0x01FE, 3, 2);
    emu_reset(emu);
    CHECK_EQ(emu->stop, EMU_RUNNING);
    CHECK_EQ(emu->exit_code, 0);
}

TEST(hello)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x403F, 0xC100,                  /* mov #msg, r15 */
            0x4F7E,                                  /* loop: mov.b @r15+, r14 */
            0x934E,                                  /* tst.b r14 */
            0x2406,                                  /* jz done */
            0xB3E2, IFG2,                            /* wait: bit.b #UCA0TXIFG, &IFG2 */
            0x27FD,                                  /* jz wait */
            0x4EC2, UCA0TXBUF,                       /* mov.b r14, &UCA0TXBUF */
            0x3FF7,                                  /* jmp loop */
            0x4382, 0x01FE);                         /* done: mov #0, &0x01fe */
    PROGRAM(0xC100, 0x6548, 0x6C6C, 0x0A6F, 0x0000); /* "Hello\n" */

    for (int n = 0; n < 1000 && emu->stop == EMU_RUNNING; n++)
        step(emu, 1);
    CHECK(strcmp(uart_output, "Hello\n") == 0);
    CHECK_EQ(emu->stop, EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 0);
}

int main(int argc, char **argv)
{
    static const Test tests[] = {
        T(tx_sends_byte),
        T(txifg_always_set),
        T(ifg2_flags_read_only),
        T(rx_one_byte_at_a_time),
        T(rx_interrupt),
        T(rx_interrupt_needs_rxie),
        T(other_usci_registers_are_memory),
        T(stop_word),
        T(stop_byte),
        T(reset_clears_stop),
        T(hello),
    };
    return run_tests(tests, sizeof tests / sizeof *tests, argc, argv);
}
