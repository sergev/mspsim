#include "harness.h"

/*
 * Common layout: main loop at 0xC000, ISR at 0xC010 (mov #0x1234, r4; reti),
 * vectors 2 and 3 point to the ISR.
 */
#define MAIN 0xC000
#define ISR  0xC010

static Emulator *setup(void)
{
    Emulator *emu = emu_new();

    PROGRAM(MAIN, 0xD232,        /* eint */
            0x3FFF);             /* jmp $ */
    PROGRAM(ISR, 0x4034, 0x1234, /* mov #0x1234, r4 */
            0x1300);             /* reti */
    poke(VECTOR_TABLE + 2 * 2, ISR);
    poke(VECTOR_TABLE + 2 * 3, ISR);
    return emu;
}

TEST(eint_sets_gie)
{
    Emulator *emu = setup();

    step(emu, 1);
    CHECK(emu->cpu->sr.GIE);
    CHECK_EQ(emu->cpu->pc, MAIN + 2);
}

TEST(entry_pushes_pc_and_sr)
{
    Emulator *emu = setup();
    Cpu *cpu      = emu->cpu;

    cpu->sr.carry = 1;
    step(emu, 1); /* eint */
    cpu_set_irq(emu, 2, true);
    step(emu, 1); /* jmp, then accept */

    CHECK_EQ(cpu->pc, ISR);
    CHECK_EQ(cpu->sp, 0x03FC);
    CHECK_EQ(peek(0x03FE), MAIN + 2); /* return address */
    CHECK_EQ(peek(0x03FC), 0x0009);   /* GIE | C */
}

TEST(entry_clears_sr_except_scg0)
{
    Emulator *emu = setup();
    Cpu *cpu      = emu->cpu;

    step(emu, 1);
    set_sr_value(emu, 0x01CF); /* V SCG1 SCG0 GIE N Z C */
    cpu_set_irq(emu, 2, true);
    step(emu, 1);

    CHECK_EQ(cpu->pc, ISR);
    CHECK_EQ(sr_to_value(emu), 0x0040); /* SCG0 only */
}

TEST(reti_restores_pc_and_sr)
{
    Emulator *emu = setup();
    Cpu *cpu      = emu->cpu;

    cpu->sr.carry = 1;
    step(emu, 1);
    cpu_set_irq(emu, 2, true);
    step(emu, 1);
    cpu_set_irq(emu, 2, false);
    step(emu, 2); /* mov, reti */

    CHECK_EQ(cpu->r4, 0x1234);
    CHECK_EQ(cpu->pc, MAIN + 2);
    CHECK_EQ(cpu->sp, 0x0400);
    CHECK(cpu->sr.GIE);
    CHECK(cpu->sr.carry);
}

TEST(request_held_while_gie_clear)
{
    Emulator *emu = setup();
    Cpu *cpu      = emu->cpu;

    cpu_set_irq(emu, 3, true); /* before eint */
    step(emu, 1);              /* eint: accepted right after */
    CHECK_EQ(cpu->pc, ISR);

    cpu_set_irq(emu, 3, false);
    step(emu, 2);
    cpu->sr.GIE = 0;
    cpu_set_irq(emu, 3, true);
    step(emu, 3);
    CHECK_EQ(cpu->pc, MAIN + 2); /* still masked */
    CHECK(cpu->irq_pending & (1u << 3));
}

TEST(deasserted_request_not_taken)
{
    Emulator *emu = setup();

    step(emu, 1);
    cpu_set_irq(emu, 2, true);
    cpu_set_irq(emu, 2, false);
    step(emu, 1);
    CHECK_EQ(emu->cpu->pc, MAIN + 2);
}

TEST(higher_vector_wins)
{
    Emulator *emu = setup();
    Cpu *cpu      = emu->cpu;

    PROGRAM(0xC020, 0x4035, 0x5678, /* mov #0x5678, r5 */
            0x1300);                /* reti */
    poke(VECTOR_TABLE + 2 * 3, 0xC020);

    step(emu, 1);
    cpu_set_irq(emu, 2, true);
    cpu_set_irq(emu, 3, true);
    step(emu, 1);
    CHECK_EQ(cpu->pc, 0xC020); /* vector 3 first */

    cpu_set_irq(emu, 3, false);
    step(emu, 2); /* mov, reti, then vector 2 */
    CHECK_EQ(cpu->pc, ISR);
    CHECK_EQ(cpu->r5, 0x5678);
}

TEST(nmi_ignores_gie_and_autoclears)
{
    Emulator *emu = setup();
    Cpu *cpu      = emu->cpu;

    poke(VECTOR_TABLE + 2 * NMI_IRQ, ISR);
    cpu->pc = MAIN + 2; /* skip eint */
    cpu_set_irq(emu, NMI_IRQ, true);
    step(emu, 1);

    CHECK_EQ(cpu->pc, ISR);
    CHECK(!(cpu->irq_pending & (1u << NMI_IRQ)));
}

TEST(reset_vector_not_requestable)
{
    Emulator *emu = setup();

    cpu_set_irq(emu, RESET_IRQ, true);
    CHECK_EQ(emu->cpu->irq_pending, 0);
}

TEST(wakeup_from_cpuoff)
{
    Emulator *emu = emu_new();
    Cpu *cpu      = emu->cpu;

    PROGRAM(MAIN, 0xD032, 0x0018,        /* bis #0x18, sr: LPM0 + GIE */
            0x3FFF);                     /* jmp $ */
    PROGRAM(ISR, 0xC0B1, 0x0010, 0x0000, /* bic #0x10, 0(sp): stay awake */
            0x1300);                     /* reti */
    poke(VECTOR_TABLE + 2 * 2, ISR);

    step(emu, 1);
    CHECK(cpu->sr.CPUOFF);
    CHECK(cpu->sr.GIE);
    uint16_t pc = cpu->pc;
    step(emu, 5);
    CHECK_EQ(cpu->pc, pc); /* sleeping */

    cpu_set_irq(emu, 2, true);
    step(emu, 1);
    CHECK_EQ(cpu->pc, ISR);
    CHECK(!cpu->sr.CPUOFF);

    cpu_set_irq(emu, 2, false);
    step(emu, 2); /* bic, reti */
    CHECK_EQ(cpu->pc, MAIN + 4);
    CHECK(!cpu->sr.CPUOFF);
    CHECK(cpu->sr.GIE);
}

TEST(cycle_counting)
{
    Emulator *emu = setup();
    Cpu *cpu      = emu->cpu;

    step(emu, 1); /* eint: 4 */
    cpu_set_irq(emu, 2, true);
    step(emu, 1); /* jmp + entry: 4 + 6 */
    CHECK_EQ(cpu->cycles, 14);

    cpu_set_irq(emu, 2, false);
    cpu->sr.CPUOFF = 1;
    step(emu, 3); /* idle: 1 each */
    CHECK_EQ(cpu->cycles, 17);
}

int main(int argc, char **argv)
{
    static const Test tests[] = {
        T(eint_sets_gie),
        T(entry_pushes_pc_and_sr),
        T(entry_clears_sr_except_scg0),
        T(reti_restores_pc_and_sr),
        T(request_held_while_gie_clear),
        T(deasserted_request_not_taken),
        T(higher_vector_wins),
        T(nmi_ignores_gie_and_autoclears),
        T(reset_vector_not_requestable),
        T(wakeup_from_cpuoff),
        T(cycle_counting),
    };
    return run_tests(tests, sizeof tests / sizeof *tests, argc, argv);
}
