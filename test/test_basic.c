#include "harness.h"

TEST(arith_sequence)
{
    Emulator *emu = emu_new();
    Cpu *cpu      = emu->cpu;

    PROGRAM(0xC000, 0x4034, 0x0005, /* mov #5, r4 */
            0x5404,                 /* add r4, r4 */
            0x8314,                 /* sub #1, r4 */
            0x3FFF);                /* jmp $ */
    step(emu, 3);

    CHECK_EQ(cpu->r4, 9);
    CHECK_EQ(cpu->pc, 0xC008);
    CHECK(cpu->sr & SR_C); /* no borrow */
    CHECK(!(cpu->sr & SR_Z));
    CHECK(!(cpu->sr & SR_N));
    CHECK(!(cpu->sr & SR_V));
}

TEST(jmp_self_loops)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x3FFF); /* jmp $ */
    step(emu, 10);
    CHECK_EQ(emu->cpu->pc, 0xC000);
}

TEST(reset_state)
{
    Emulator *emu = emu_new();
    Cpu *cpu      = emu->cpu;

    CHECK_EQ(cpu->pc, 0xC000);
    CHECK_EQ(cpu->sp, 0x0400);
    CHECK_EQ(emu->cpu->sr, 0);
    CHECK_EQ(cpu->cycles, 0);
    CHECK_EQ(cpu->irq_pending, 0);
}

int main(int argc, char **argv)
{
    static const Test tests[] = {
        T(arith_sequence),
        T(jmp_self_loops),
        T(reset_state),
    };
    return run_tests(tests, sizeof tests / sizeof *tests, argc, argv);
}
