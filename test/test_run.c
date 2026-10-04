#include "debugger/debugger.h"
#include "harness.h"
#include "memory/memory.h"
#include "uart/uart.h"

TEST(stop_register)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x40B2, 0x0003, 0x01FE); /* mov #3, &0x01fe */
    CHECK_EQ(emu_run(emu, 0), EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 3);
}

TEST(max_cycles)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x3FFF); /* jmp $ */
    CHECK_EQ(emu_run(emu, 100), EMU_MAX_CYCLES);
    CHECK(emu->cpu->cycles >= 100 && emu->cpu->cycles < 104);
}

TEST(illegal_instruction)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x4304, /* mov #0, r4 */
            0x0000);        /* illegal */
    CHECK_EQ(emu_run(emu, 0), EMU_ILLEGAL);
    CHECK_EQ(emu->cpu->pc, 0xC002);
}

TEST(illegal_format_ii)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x13B0, 0x1234); /* opcode 7 with an immediate */
    CHECK_EQ(emu_run(emu, 0), EMU_ILLEGAL);
    CHECK_EQ(emu->cpu->pc, 0xC000);
}

TEST(sleep_without_gie)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0xD032, 0x0010); /* bis #CPUOFF, sr */
    CHECK_EQ(emu_run(emu, 0), EMU_SLEEP);
}

TEST(sleep_without_wakeup_source)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0xD032, 0x0018); /* bis #CPUOFF|GIE, sr */
    CHECK_EQ(emu_run(emu, 0), EMU_SLEEP);
}

TEST(sleep_at_end_of_input)
{
    Emulator *emu = emu_new();

    mem_write(emu, IE2, UCA0RXIE, 1);
    PROGRAM(0xC000, 0xD032, 0x0018); /* bis #CPUOFF|GIE, sr */
    CHECK_EQ(emu_run(emu, 0), EMU_SLEEP);
    CHECK(emu->uart_eof);
}

TEST(input_wakes_cpu)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0xD032, 0x0018,    /* bis #CPUOFF|GIE, sr */
            0x4482, 0x01FE);           /* mov r4, &0x01fe */
    PROGRAM(0xC020, 0x4254, UCA0RXBUF, /* mov.b &UCA0RXBUF, r4 */
            0xC0B1, 0x0010, 0x0000,    /* bic #CPUOFF, 0(sp) */
            0x1300);                   /* reti */
    poke(VECTOR_TABLE + 2 * UART_RX_IRQ, 0xC020);
    mem_write(emu, IE2, UCA0RXIE, 1);
    emu->uart_poll_at = 10000; /* input shows up while asleep */
    uart_input        = "z";

    CHECK_EQ(emu_run(emu, 0), EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 'z');
    CHECK(emu->cpu->cycles >= 10000);
}

TEST(breakpoint)
{
    Emulator *emu = emu_new();
    Cpu *cpu      = emu->cpu;

    PROGRAM(0xC000, 0x4314, /* mov #1, r4 */
            0x4325,         /* mov #2, r5 */
            0x3FFF);        /* jmp $ */
    exec_cmd(emu, "break C002");

    CHECK_EQ(emu_run(emu, 0), EMU_BREAKPOINT);
    CHECK_EQ(cpu->pc, 0xC002);
    CHECK_EQ(cpu->r4, 1);
    CHECK_EQ(cpu->r5, 0);

    CHECK_EQ(emu_run(emu, 100), EMU_MAX_CYCLES); /* resumes past it */
    CHECK_EQ(cpu->r5, 2);
}

TEST(break_request)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x3FFF); /* jmp $ */
    emu->break_request = 1;
    CHECK_EQ(emu_run(emu, 0), EMU_INTERRUPT);
    CHECK_EQ(emu->break_request, 0);
    CHECK_EQ(emu->cpu->cycles, 0);
}

int main(int argc, char **argv)
{
    static const Test tests[] = {
        T(stop_register),         T(max_cycles),        T(illegal_instruction),
        T(illegal_format_ii),     T(sleep_without_gie), T(sleep_without_wakeup_source),
        T(sleep_at_end_of_input), T(input_wakes_cpu),   T(breakpoint),
        T(break_request),
    };
    return run_tests(tests, sizeof tests / sizeof *tests, argc, argv);
}
