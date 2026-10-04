/*
 * The test assembler (asm.c) against known encodings, and the binutils
 * simulator test add.s run through it.
 */
#include <string.h>

#include "asm.h"
#include "harness.h"

/* Assemble at ASM_TEXT and compare the words produced. */
#define ENCODES(source, ...)                                                                  \
    do {                                                                                      \
        const uint16_t _w[] = { __VA_ARGS__ };                                                \
        Emulator *_e        = emu_new();                                                      \
        assemble(_e, source);                                                                 \
        for (size_t _i = 0; _i < sizeof _w / sizeof *_w; _i++)                                \
            if (peek(ASM_TEXT + 2 * _i) != _w[_i]) {                                          \
                printf("  %s:%d: '%s' word %zu == %04x, expected %04x\n", __FILE__, __LINE__, \
                       source, _i, peek(ASM_TEXT + 2 * _i), _w[_i]);                          \
                test_failed = 1;                                                              \
            }                                                                                 \
    } while (0)

/* Assembly must fail with a message containing text. */
static void rejects(const char *source, const char *text)
{
    Emulator *emu = emu_new();
    char err[256] = "";

    if (asm_text(emu, source, err, sizeof err) == 0 || strstr(err, text) == NULL) {
        printf("  '%s': got '%s', expected '%s'\n", source, err, text);
        test_failed = 1;
    }
}

TEST(constant_generators)
{
    ENCODES("mov #0, r5", 0x4305);
    ENCODES("mov #1, r5", 0x4315);
    ENCODES("mov #2, r5", 0x4325);
    ENCODES("mov #4, r5", 0x4225);
    ENCODES("mov #8, r5", 0x4235);
    ENCODES("mov #-1, r5", 0x4335);
    ENCODES("mov #0xffff, r5", 0x4335);
    ENCODES("mov.b #0xff, r5", 0x4375);
    ENCODES("mov #3, r5", 0x4035, 0x0003);
    ENCODES("mov #0x1234, r5", 0x4035, 0x1234);
}

TEST(addressing_modes)
{
    ENCODES("mov r4, r5", 0x4405);
    ENCODES("mov @r4, r5", 0x4425);
    ENCODES("mov @r4+, r5", 0x4435);
    ENCODES("add @r4+, 2(r5)", 0x54B5, 0x0002);
    ENCODES("mov -2(r4), r5", 0x4415, 0xFFFE);
    ENCODES("mov &0x0200, &0x0202", 0x4292, 0x0200, 0x0202);
    ENCODES("mov 0xc010, r5", 0x4015, 0x000E); /* symbolic: relative to the word */
    ENCODES("mov r5, 0xc010", 0x4580, 0x000E);
    ENCODES("mov r4, @r5", 0x4485, 0x0000); /* @Rn destination is 0(Rn) */
    ENCODES("mov.b r4, 3(r5)", 0x44C5, 0x0003);
    ENCODES("mov sp, pc", 0x4100);
    ENCODES("MOV.W R4, SR", 0x4402);
}

TEST(single_operand_and_jumps)
{
    ENCODES("rrc r5", 0x1005);
    ENCODES("swpb r5", 0x1085);
    ENCODES("rra.b r5", 0x1145);
    ENCODES("sxt r5", 0x1185);
    ENCODES("push.b r5", 0x1245);
    ENCODES("push #0x1234", 0x1230, 0x1234);
    ENCODES("call #0xc010", 0x12B0, 0xC010);
    ENCODES("reti", 0x1300);
    ENCODES("label: jmp label", 0x3FFF);
    ENCODES("jmp $", 0x3FFF);
    ENCODES("jne 1f\nnop\n1: jz 1b", 0x2001, 0x4303, 0x27FF);
    ENCODES("jlo $+4\njhs $+4\njn $\njge $\njl $", 0x2801, 0x2C01, 0x33FF, 0x37FF, 0x3BFF);
}

TEST(emulated_instructions)
{
    ENCODES("nop", 0x4303);
    ENCODES("ret", 0x4130);
    ENCODES("pop r5", 0x4135);
    ENCODES("br #0xc000", 0x4030, 0xC000);
    ENCODES("br r5", 0x4500);
    ENCODES("clr.b r5", 0x4345);
    ENCODES("inc r5", 0x5315);
    ENCODES("incd r5", 0x5325);
    ENCODES("dec r5", 0x8315);
    ENCODES("decd r5", 0x8325);
    ENCODES("tst r5", 0x9305);
    ENCODES("inv r5", 0xE335);
    ENCODES("rla r5", 0x5505);
    ENCODES("rlc.b r5", 0x6545);
    ENCODES("adc r5", 0x6305);
    ENCODES("sbc r5", 0x7305);
    ENCODES("dadc r5", 0xA305);
    ENCODES("setc\nsetz\nsetn\nclrc\nclrz\nclrn", 0xD312, 0xD322, 0xD222, 0xC312, 0xC322, 0xC222);
    ENCODES("eint\ndint", 0xD232, 0xC232);
}

TEST(directives)
{
    ENCODES(".set X, 2 * (3 + 4)\n.word X, X - 1\n.byte 1, 2\n.even\n.word $", 14, 13, 0x0201,
            ASM_TEXT + 6);
    ENCODES("/* block\n comment */ mov r4, r5 ; trailing\n# line comment\n", 0x4405);

    Emulator *emu = emu_new();
    assemble(emu,
             ".data\nbuf: .word 0x1234\n.text\nmain: mov &buf, r5\n"
             ".section .vectors, \"a\"\n.word 0, 0, 0, 0, 0, 0, 0, 0\n"
             ".word 0, 0, 0, 0, 0, 0, 0, main");
    CHECK_EQ(peek(ASM_DATA), 0x1234);
    CHECK_EQ(peek(ASM_TEXT + 2), ASM_DATA);
    CHECK_EQ(peek(0xFFFE), ASM_TEXT);
}

TEST(errors)
{
    rejects("mov r5", "needs two operands");
    rejects("mov r4, #1", "not a destination");
    rejects("frob r5", "unknown instruction frob");
    rejects("mov #nowhere, r5", "undefined symbol nowhere");
    rejects("nop\njmp 0xd000", "line 2: jump target out of range");
    rejects(".set X, 1\n.word X +", "bad expression");
    rejects("mov.l r4, r5", "bad size suffix");
}

TEST(binutils_add)
{
    Emulator *emu = emu_new();
    char err[256];

    if (asm_file(emu, TEST_SOURCE_DIR "/binutils/add.s", err, sizeof err) < 0) {
        printf("  add.s: %s\n", err);
        test_failed = 1;
        return;
    }
    emu_reset(emu);
    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 0); /* pass */
}

int main(int argc, char **argv)
{
    static const Test tests[] = {
        T(constant_generators),   T(addressing_modes), T(single_operand_and_jumps),
        T(emulated_instructions), T(directives),       T(errors),
        T(binutils_add),
    };
    return run_tests(tests, sizeof tests / sizeof *tests, argc, argv);
}
