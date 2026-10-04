/*
 * The openMSP430 instruction tests (test/openmsp430/, see README there).
 *
 * Each NAME.s43 is assembled with test/asm.c and run. NAME.chk, converted
 * from the openMSP430 Verilog checker, lists what to verify:
 *   at XXXX               run until r15 == XXXX
 *   rN = XXXX message     register value (!= for "must differ")
 *   rN.B = X message      one bit of a register
 *   memXXXX = XXXX msg    memory word
 *   irq MASK N            raise IRQ lines MASK for N cycles
 *   nmi                   request an NMI
 */
#include <stdlib.h>
#include <string.h>

#include "asm.h"
#include "harness.h"

#define MAX_STEPS   200000 /* per checkpoint */
#define MAX_REPORTS 10     /* failures printed per test */

static int reports;

static void fail(const char *name, uint16_t at, const char *fmt, const char *what, unsigned actual,
                 unsigned expected)
{
    test_failed = 1;
    if (reports++ < MAX_REPORTS) {
        printf("  %s, at %04x: ", name, at);
        printf(fmt, what, actual, expected);
        printf("\n");
    }
}

static void run_omsp(const char *name)
{
    Emulator *emu = emu_new();
    Cpu *cpu      = emu->cpu;
    char path[512], err[256], line[256];
    uint16_t at = 0, irq_mask = 0;
    uint64_t irq_off = 0;
    FILE *chk;

    reports = 0;
    snprintf(path, sizeof path, "%s/openmsp430/%s.s43", TEST_SOURCE_DIR, name);
    if (asm_file(emu, path, err, sizeof err) < 0) {
        printf("  %s.s43: %s\n", name, err);
        test_failed = 1;
        return;
    }
    emu_reset(emu);

    snprintf(path, sizeof path, "%s/openmsp430/%s.chk", TEST_SOURCE_DIR, name);
    chk = fopen(path, "r");
    if (chk == NULL) {
        printf("  cannot open %s\n", path);
        test_failed = 1;
        return;
    }

    while (fgets(line, sizeof line, chk)) {
        char lhs[16], op[4];
        unsigned value, mask, cycles;
        int msg = 0;

        line[strcspn(line, "\n")] = 0;
        if (line[0] == 0 || line[0] == '#')
            continue;

        if (sscanf(line, "at %x", &value) == 1) {
            at = value;
            for (int steps = 0; cpu->r15 != at; steps++) {
                if (steps == MAX_STEPS || emu->stop != EMU_RUNNING) {
                    printf("  %s: checkpoint %04x not reached (pc %04x, %s)\n", name, at, cpu->pc,
                           emu_stop_reason(emu->stop));
                    test_failed = 1;
                    fclose(chk);
                    return;
                }
                cpu_step(emu);
                if (irq_mask && cpu->cycles >= irq_off) {
                    for (int i = 0; i < 16; i++)
                        if (irq_mask & (1u << i))
                            cpu_set_irq(emu, i, false);
                    irq_mask = 0;
                }
            }
            continue;
        }
        if (sscanf(line, "irq %x %u", &mask, &cycles) == 2) {
            for (int i = 0; i < 16; i++)
                if (mask & (1u << i))
                    cpu_set_irq(emu, i, true);
            irq_mask = mask;
            irq_off  = cpu->cycles + cycles;
            continue;
        }
        if (strcmp(line, "nmi") == 0) {
            cpu_set_irq(emu, NMI_IRQ, true);
            continue;
        }
        if (sscanf(line, "%15s %3s %x %n", lhs, op, &value, &msg) < 3) {
            printf("  %s.chk: bad line '%s'\n", name, line);
            test_failed = 1;
            continue;
        }

        unsigned actual, addr, reg, bit;
        if (sscanf(lhs, "mem%x", &addr) == 1)
            actual = emu->mem[addr] | emu->mem[addr + 1] << 8;
        else if (sscanf(lhs, "r%u.%u", &reg, &bit) == 2)
            actual = (cpu->r[reg] >> bit) & 1;
        else if (sscanf(lhs, "r%u", &reg) == 1)
            actual = cpu->r[reg];
        else {
            printf("  %s.chk: bad operand '%s'\n", name, lhs);
            test_failed = 1;
            continue;
        }

        if (strcmp(op, "=") == 0 && actual != value)
            fail(name, at, "%s == %04x, expected %04x", line + msg, actual, value);
        else if (strcmp(op, "!=") == 0 && actual == value)
            fail(name, at, "%s == %04x, expected anything but %04x", line + msg, actual, value);
    }
    fclose(chk);
    if (reports > MAX_REPORTS)
        printf("  %s: %d more failures\n", name, reports - MAX_REPORTS);
}

#define OMSP(id, file)  \
    TEST(id)            \
    {                   \
        run_omsp(file); \
    }

OMSP(two_op_add, "two-op_add")
OMSP(two_op_add_b, "two-op_add-b")
OMSP(two_op_add_rom_rd, "two-op_add_rom-rd")
OMSP(two_op_addc, "two-op_addc")
OMSP(two_op_and, "two-op_and")
OMSP(two_op_bic, "two-op_bic")
OMSP(two_op_bis, "two-op_bis")
OMSP(two_op_bit, "two-op_bit")
OMSP(two_op_cmp, "two-op_cmp")
OMSP(two_op_dadd, "two-op_dadd")
OMSP(two_op_mov, "two-op_mov")
OMSP(two_op_mov_b, "two-op_mov-b")
OMSP(two_op_sub, "two-op_sub")
OMSP(two_op_subc, "two-op_subc")
OMSP(two_op_xor, "two-op_xor")
OMSP(sing_op_call, "sing-op_call")
OMSP(sing_op_call_rom_rd, "sing-op_call_rom-rd")
OMSP(sing_op_push, "sing-op_push")
OMSP(sing_op_push_rom_rd, "sing-op_push_rom-rd")
OMSP(sing_op_reti, "sing-op_reti")
OMSP(sing_op_rra, "sing-op_rra")
OMSP(sing_op_rrc, "sing-op_rrc")
OMSP(sing_op_swpb, "sing-op_swpb")
OMSP(sing_op_sxt, "sing-op_sxt")
OMSP(c_jump_jc, "c-jump_jc")
OMSP(c_jump_jeq, "c-jump_jeq")
OMSP(c_jump_jge, "c-jump_jge")
OMSP(c_jump_jl, "c-jump_jl")
OMSP(c_jump_jmp, "c-jump_jmp")
OMSP(c_jump_jn, "c-jump_jn")
OMSP(c_jump_jnc, "c-jump_jnc")
OMSP(c_jump_jne, "c-jump_jne")

int main(int argc, char **argv)
{
    static const Test tests[] = {
        T(two_op_add),          T(two_op_add_b), T(two_op_add_rom_rd),   T(two_op_addc),
        T(two_op_and),          T(two_op_bic),   T(two_op_bis),          T(two_op_bit),
        T(two_op_cmp),          T(two_op_dadd),  T(two_op_mov),          T(two_op_mov_b),
        T(two_op_sub),          T(two_op_subc),  T(two_op_xor),          T(sing_op_call),
        T(sing_op_call_rom_rd), T(sing_op_push), T(sing_op_push_rom_rd), T(sing_op_reti),
        T(sing_op_rra),         T(sing_op_rrc),  T(sing_op_swpb),        T(sing_op_sxt),
        T(c_jump_jc),           T(c_jump_jeq),   T(c_jump_jge),          T(c_jump_jl),
        T(c_jump_jmp),          T(c_jump_jn),    T(c_jump_jnc),          T(c_jump_jne),
    };
    return run_tests(tests, sizeof tests / sizeof *tests, argc, argv);
}
