#include "harness.h"
#include "mem/memory.h"

TEST(word_access_ignores_bit0)
{
    Emulator *emu = emu_new();

    mem_write(emu, 0x0201, 0x1234, 2);
    CHECK_EQ(emu->mem[0x0200], 0x34);
    CHECK_EQ(emu->mem[0x0201], 0x12);
    CHECK_EQ(mem_read(emu, 0x0201, 2, ACC_DATA), 0x1234);
}

TEST(byte_access)
{
    Emulator *emu = emu_new();

    mem_write(emu, 0x0201, 0xAB, 1);
    CHECK_EQ(mem_read(emu, 0x0201, 1, ACC_DATA), 0xAB);
    CHECK_EQ(peek(0x0200), 0xAB00);
}

TEST(fresh_memory)
{
    emu_new();

    CHECK_EQ(peek(0x0200), 0);
    CHECK_EQ(peek(0xC000), 0xFFFF); /* erased flash */
}

TEST(sr_reserved_bits_read_as_zero)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x4032, 0xFE01); /* mov #0xfe01, sr */
    step(emu, 1);
    CHECK_EQ(emu->cpu->sr, 0x0001);
}

TEST(symbolic_source_and_destination)
{
    Emulator *emu = emu_new();

    poke(0x0200, 0xBEEF);
    PROGRAM(0xC000, 0x4090, (uint16_t)(0x0200 - 0xC002),
            (uint16_t)(0x0210 - 0xC004)); /* mov X, Y */
    step(emu, 1);
    CHECK_EQ(peek(0x0210), 0xBEEF);
    CHECK_EQ(emu->cpu->pc, 0xC006);
}

TEST(push_symbolic)
{
    Emulator *emu = emu_new();

    poke(0x0200, 0xBEEF);
    PROGRAM(0xC000, 0x1210, (uint16_t)(0x0200 - 0xC002)); /* push X */
    step(emu, 1);
    CHECK_EQ(emu->cpu->sp, 0x03FE);
    CHECK_EQ(peek(0x03FE), 0xBEEF);
}

TEST(autoincrement_byte)
{
    Emulator *emu = emu_new();
    Cpu *cpu      = emu->cpu;

    poke(0x0200, 0x3412);
    cpu->r5 = 0x0200;
    PROGRAM(0xC000, 0x4576, /* mov.b @r5+, r6 */
            0x4577);        /* mov.b @r5+, r7 */
    step(emu, 2);
    CHECK_EQ(cpu->r5, 0x0202);
    CHECK_EQ(cpu->r6, 0x12);
    CHECK_EQ(cpu->r7, 0x34);
}

TEST(sp_autoincrement_byte_steps_by_2)
{
    Emulator *emu = emu_new();
    Cpu *cpu      = emu->cpu;

    poke(0x03FE, 0x0077);
    cpu->sp = 0x03FE;
    PROGRAM(0xC000, 0x4176); /* mov.b @sp+, r6 */
    step(emu, 1);
    CHECK_EQ(cpu->sp, 0x0400);
    CHECK_EQ(cpu->r6, 0x77);
}

TEST(byte_write_clears_register_high_byte)
{
    Emulator *emu = emu_new();

    emu->cpu->r5 = 0xFFFF;
    PROGRAM(0xC000, 0x4075, 0x0012); /* mov.b #0x12, r5 */
    step(emu, 1);
    CHECK_EQ(emu->cpu->r5, 0x0012);
}

TEST(cmp_byte_leaves_register)
{
    Emulator *emu = emu_new();

    emu->cpu->r5 = 0x1234;
    PROGRAM(0xC000, 0x9075, 0x0034); /* cmp.b #0x34, r5 */
    step(emu, 1);
    CHECK_EQ(emu->cpu->r5, 0x1234);
    CHECK(emu->cpu->sr & SR_Z);
}

TEST(read_modify_write_memory)
{
    Emulator *emu = emu_new();

    poke(0x0200, 0x00FF);
    PROGRAM(0xC000, 0x5392, 0x0200, /* add #1, &0x0200 */
            0x53D2, 0x0201);        /* add.b #1, &0x0201 */
    step(emu, 2);
    CHECK_EQ(peek(0x0200), 0x0200); /* 0x00ff + 1, then high byte + 1 */
    CHECK_EQ(emu->cpu->pc, 0xC008);
}

TEST(call_and_ret)
{
    Emulator *emu = emu_new();
    Cpu *cpu      = emu->cpu;

    PROGRAM(0xC000, 0x12B0, 0xC010); /* call #0xc010 */
    PROGRAM(0xC010, 0x4130);         /* ret */
    step(emu, 1);
    CHECK_EQ(cpu->pc, 0xC010);
    CHECK_EQ(cpu->sp, 0x03FE);
    CHECK_EQ(peek(0x03FE), 0xC004);
    step(emu, 1);
    CHECK_EQ(cpu->pc, 0xC004);
    CHECK_EQ(cpu->sp, 0x0400);
}

TEST(r3_ignores_writes)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x4033, 0x1234); /* mov #0x1234, r3 */
    step(emu, 1);
    CHECK_EQ(emu->cpu->cg2, 0);
}

int main(int argc, char **argv)
{
    static const Test tests[] = {
        T(word_access_ignores_bit0),
        T(byte_access),
        T(fresh_memory),
        T(sr_reserved_bits_read_as_zero),
        T(symbolic_source_and_destination),
        T(push_symbolic),
        T(autoincrement_byte),
        T(sp_autoincrement_byte_steps_by_2),
        T(byte_write_clears_register_high_byte),
        T(cmp_byte_leaves_register),
        T(read_modify_write_memory),
        T(call_and_ret),
        T(r3_ignores_writes),
    };
    return run_tests(tests, sizeof tests / sizeof *tests, argc, argv);
}
