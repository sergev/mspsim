/*
 * Instruction semantics and cycle counts, written in assembly (asm.h).
 * The openMSP430 suite (test_openmsp430.c) covers the addressing modes
 * in depth; these tables cover flags, byte forms and timing.
 */
#include "harness.h"

#define C SR_C
#define Z SR_Z
#define N SR_N
#define V SR_V

typedef struct {
    const char *op;
    uint16_t src, dst, sr_in;
    uint16_t result, sr_out;
} Alu;

/* op #src, r5 with r5 = dst and SR = sr_in */
static const Alu two_operand[] = {
    { "add", 0x0001, 0x7FFF, 0, 0x8000, N | V },     { "add", 0x0001, 0xFFFF, 0, 0x0000, Z | C },
    { "add", 0x8000, 0x8000, 0, 0x0000, Z | C | V }, { "add", 0x1234, 0x1111, C, 0x2345, 0 },
    { "addc", 0x0000, 0xFFFF, C, 0x0000, Z | C },    { "addc", 0x0001, 0x0001, C, 0x0003, 0 },
    { "addc", 0x7FFF, 0x0000, C, 0x8000, N | V },    { "sub", 0x0003, 0x0005, 0, 0x0002, C },
    { "sub", 0x0005, 0x0003, C, 0xFFFE, N },         { "sub", 0x0001, 0x8000, 0, 0x7FFF, C | V },
    { "sub", 0x0005, 0x0005, 0, 0x0000, Z | C },     { "subc", 0x0003, 0x0005, 0, 0x0001, C },
    { "subc", 0x0003, 0x0005, C, 0x0002, C },        { "subc", 0x0000, 0x0000, 0, 0xFFFF, N },
    { "cmp", 0x0005, 0x0003, 0, 0x0003, N },         { "cmp", 0x0003, 0x0003, 0, 0x0003, Z | C },
    { "cmp", 0x8000, 0x7FFF, 0, 0x7FFF, N | V },     { "dadd", 0x0001, 0x0099, 0, 0x0100, 0 },
    { "dadd", 0x0001, 0x9999, 0, 0x0000, Z | C },    { "dadd", 0x0000, 0x0009, C, 0x0010, 0 },
    { "and", 0x00FF, 0x8F0F, V, 0x000F, C },         { "and", 0xFF00, 0x80FF, 0, 0x8000, N | C },
    { "and", 0x0F00, 0x00FF, C, 0x0000, Z },         { "bit", 0x8000, 0x8001, 0, 0x8001, N | C },
    { "xor", 0x8000, 0x8001, 0, 0x0001, V | C },     { "xor", 0x1234, 0x1234, 0, 0x0000, Z },
    { "bic", 0x00F0, 0x00FF, C | V, 0x000F, C | V }, { "bis", 0x00F0, 0x000F, 0, 0x00FF, 0 },
    { "mov", 0x0000, 0x1234, N, 0x0000, N },         { "add.b", 0x0001, 0x007F, 0, 0x0080, N | V },
    { "add.b", 0x0001, 0x12FF, 0, 0x0000, Z | C },   { "sub.b", 0x0001, 0x0080, 0, 0x007F, C | V },
    { "subc.b", 0x0001, 0x0000, C, 0x00FF, N },      { "cmp.b", 0x0001, 0x1200, 0, 0x1200, N },
    { "dadd.b", 0x0001, 0x0099, 0, 0x0000, Z | C },  { "xor.b", 0x0080, 0x0081, 0, 0x0001, V | C },
    { "bit.b", 0x0080, 0x8080, 0, 0x8080, N | C },   { "mov.b", 0x1234, 0xFFFF, 0, 0x0034, 0 },
};

/* op r5 with r5 = dst (src unused) */
static const Alu single_operand[] = {
    { "rrc", 0, 0x0001, C, 0x8000, C | N },      { "rrc", 0, 0x0002, V, 0x0001, 0 },
    { "rrc.b", 0, 0x1281, 0, 0x0040, C },        { "rrc.b", 0, 0x0000, C, 0x0080, N },
    { "rra", 0, 0x8001, 0, 0xC000, C | N },      { "rra", 0, 0x0001, V, 0x0000, Z | C },
    { "rra.b", 0, 0x0081, 0, 0x00C0, C | N },    { "sxt", 0, 0x0080, 0, 0xFF80, N | C },
    { "sxt", 0, 0x7F00, V, 0x0000, Z },          { "sxt", 0, 0x0012, 0, 0x0012, C },
    { "swpb", 0, 0x1234, C | V, 0x3412, C | V },
};

static void run_alu(const Alu *t, size_t n, bool two)
{
    for (size_t i = 0; i < n; i++) {
        char src[128];
        Emulator *emu = emu_new();

        if (two)
            snprintf(src, sizeof src, "mov #%u, sr\nmov #%u, r5\n%s #%u, r5", t[i].sr_in, t[i].dst,
                     t[i].op, t[i].src);
        else
            snprintf(src, sizeof src, "mov #%u, sr\nmov #%u, r5\n%s r5", t[i].sr_in, t[i].dst,
                     t[i].op);
        assemble(emu, src);
        step(emu, 3);
        if (emu->cpu->r5 != t[i].result || emu->cpu->sr != t[i].sr_out) {
            printf("  %s #%04x, %04x (sr %04x): got %04x sr %04x, expected %04x sr %04x\n", t[i].op,
                   t[i].src, t[i].dst, t[i].sr_in, emu->cpu->r5, emu->cpu->sr, t[i].result,
                   t[i].sr_out);
            test_failed = 1;
        }
    }
}

TEST(two_operand_flags)
{
    run_alu(two_operand, sizeof two_operand / sizeof *two_operand, true);
}

TEST(single_operand_flags)
{
    run_alu(single_operand, sizeof single_operand / sizeof *single_operand, false);
}

TEST(emulated_instructions)
{
    Emulator *emu = emu_new();
    Cpu *cpu      = emu->cpu;

    assemble(emu,
             "        mov     #0x0400, sp\n"
             "        mov     #0x00ff, r4\n"
             "        inc.b   r4              ; 0x00, C\n"
             "        adc     r4              ; 0x01\n"
             "        incd    r4              ; 0x03\n"
             "        dec     r4              ; 0x02\n"
             "        decd    r4              ; 0x00, Z\n"
             "        mov     #0x8001, r5\n"
             "        rla     r5              ; 0x0002, C, V\n"
             "        rlc     r5              ; 0x0005\n"
             "        inv     r5              ; 0xfffa\n"
             "        clrc\n"
             "        sbc     r5              ; 0xfff9\n"
             "        tst     r5              ; N\n"
             "        push    r5\n"
             "        clr     r5\n"
             "        pop     r6              ; 0xfff9\n"
             "        call    #sub\n"
             "        mov     #0x01fe, r8\n"
             "        setc\n"
             "        setz\n"
             "        setn\n"
             "        clrz\n"
             "        mov     sr, r9          ; N, C\n"
             "        br      #done\n"
             "        mov     #0xdead, r7\n"
             "sub:    mov     #0x1234, r7\n"
             "        ret\n"
             "done:   mov     #0, 0(r8)       ; stop\n");
    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_EQ(cpu->r4, 0x0000);
    CHECK_EQ(cpu->r5, 0x0000);
    CHECK_EQ(cpu->r6, 0xFFF9);
    CHECK_EQ(cpu->r7, 0x1234);
    CHECK_EQ(cpu->r9, N | C);
    CHECK_EQ(cpu->sp, 0x0400);
}

TEST(rla_flags)
{
    Emulator *emu = emu_new();

    assemble(emu, "mov #0x8001, r5\nrla r5");
    step(emu, 2);
    CHECK_EQ(emu->cpu->r5, 0x0002);
    CHECK_EQ(emu->cpu->sr, C | V);
}

/* A jump's 10-bit word offset reaches -512..+511 words; offsets of 256 words and more
 * (bit 8 set) are forward too. */
TEST(jump_offsets)
{
    static const struct {
        uint16_t insn, target;
    } jumps[] = {
        { 0x3C00, 0xC002 }, /* jmp $+2 */
        { 0x3CFF, 0xC200 }, /* +255 words */
        { 0x3D00, 0xC202 }, /* +256 words */
        { 0x3D1A, 0xC236 }, /* +282 words */
        { 0x3DFF, 0xC400 }, /* +511 words, the farthest forward */
        { 0x3FFF, 0xC000 }, /* jmp $ */
        { 0x3F00, 0xBE02 }, /* -256 words */
        { 0x3E00, 0xBC02 }, /* -512 words, the farthest back */
        { 0x251A, 0xC236 }, /* jz +282 words, taken: Z is set below */
    };
    for (size_t i = 0; i < sizeof jumps / sizeof *jumps; i++) {
        Emulator *emu = emu_new();
        PROGRAM(0xC000, jumps[i].insn);
        emu->cpu->sr = Z;
        step(emu, 1);
        if (emu->cpu->pc != jumps[i].target) {
            printf("  %04x: pc %04x, expected %04x\n", jumps[i].insn, emu->cpu->pc,
                   jumps[i].target);
            test_failed = 1;
        }
    }
}

/* Cycle counts, SLAU144 tables 3-14 to 3-16. */
static const struct {
    const char *insn;
    unsigned cycles;
} timing[] = {
    /* format I: source Rn */
    { "mov r4, r5", 1 },
    { "mov r6, pc", 2 },
    { "mov r4, 0(r5)", 4 },
    { "mov r4, &0x0210", 4 },
    { "mov r4, 0x0210", 4 },
    /* constants from the generators time as Rn */
    { "mov #1, r5", 1 },
    { "mov #8, 0(r5)", 4 },
    /* @Rn, @Rn+, #N */
    { "mov @r4, r5", 2 },
    { "mov @r4, pc", 2 },
    { "mov @r4, 0(r5)", 5 },
    { "mov @r4+, r5", 2 },
    { "mov @r4+, pc", 3 },
    { "mov @r4+, 0(r5)", 5 },
    { "mov #0x1234, r5", 2 },
    { "mov #0xc100, pc", 3 },
    { "mov #0x1234, 0(r5)", 5 },
    /* x(Rn), EDE, &EDE */
    { "mov 2(r4), r5", 3 },
    { "mov &0x0200, pc", 3 },
    { "mov 0x0200, r5", 3 },
    { "mov &0x0200, &0x0202", 6 },
    { "add 2(r4), 2(r5)", 6 },
    /* format II */
    { "rra r5", 1 },
    { "rra @r4", 3 },
    { "rra @r4+", 3 },
    { "rra 2(r4)", 4 },
    { "swpb &0x0200", 4 },
    { "push r5", 3 },
    { "push #1", 3 },
    { "push @r4", 4 },
    { "push @r4+", 5 },
    { "push #0x1234", 4 },
    { "push 2(r4)", 5 },
    { "call r6", 4 },
    { "call @r4", 4 },
    { "call @r4+", 5 },
    { "call #0xc100", 5 },
    { "call 2(r4)", 5 },
    { "call &0x0200", 5 },
    { "reti", 5 },
    /* jumps, taken or not */
    { "jmp $+4", 2 },
    { "jz $+4", 2 },
};

TEST(cycle_counts)
{
    for (size_t i = 0; i < sizeof timing / sizeof *timing; i++) {
        Emulator *emu = emu_new();
        Cpu *cpu      = emu->cpu;

        assemble(emu, timing[i].insn);
        cpu->r4 = 0x0200;
        cpu->r5 = 0x0204;
        cpu->r6 = 0xC100;
        cpu->sp = 0x03FC;
        step(emu, 1);
        if (cpu->cycles != timing[i].cycles) {
            printf("  %s: %u cycles, expected %u\n", timing[i].insn, (unsigned)cpu->cycles,
                   timing[i].cycles);
            test_failed = 1;
        }
    }
}

int main(int argc, char **argv)
{
    static const Test tests[] = {
        T(two_operand_flags), T(single_operand_flags), T(emulated_instructions),
        T(rla_flags),         T(jump_offsets),          T(cycle_counts),
    };
    return run_tests(tests, sizeof tests / sizeof *tests, argc, argv);
}
