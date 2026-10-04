#include <string.h>

#include "debugger/trace.h"
#include "harness.h"

/* Send the trace to a temporary file. */
static void trace_start(Emulator *emu)
{
    emu->trace      = true;
    emu->trace_file = tmpfile();
}

/* Trace text so far; stops tracing. */
static const char *trace_text(Emulator *emu)
{
    static char buf[4096];
    size_t n;

    rewind(emu->trace_file);
    n      = fread(buf, 1, sizeof buf - 1, emu->trace_file);
    buf[n] = 0;
    fclose(emu->trace_file);
    emu->trace_file = NULL;
    emu->trace      = false;
    return buf;
}

TEST(loads_stores_and_registers)
{
    Emulator *emu = emu_new();
    Cpu *cpu      = emu->cpu;

    PROGRAM(0xC000, 0x40B2, 0x5A80, 0x0120, /* mov #0x5a80, &0x0120 */
            0x4031, 0x0300,                 /* mov #0x0300, sp */
            0x5292, 0x0200, 0x0202,         /* add &0x0200, &0x0202 */
            0x4FE5, 0x0003);                /* mov.b @r15, 3(r5) */
    poke(0x0200, 5);
    poke(0x0202, 3);
    poke(0x1234, 0x0041);
    cpu->r15 = 0x1234;
    cpu->r5  = 0x0204;

    trace_start(emu);
    step(emu, 4);
    CHECK_STR(trace_text(emu),
              "c000: 40b2 5a80 0120   mov   #0x5a80, &0x0120\n"
              "      W  [0120] <- 5a80\n"
              "c006: 4031 0300        mov   #0x0300, sp\n"
              "      SP 0400 -> 0300\n"
              "c00a: 5292 0200 0202   add   &0x0200, &0x0202\n"
              "      R  [0200] -> 0005\n"
              "      R  [0202] -> 0003\n"
              "      W  [0202] <- 0008\n"
              "c010: 4fe5 0003        mov.b @r15, 3(r5)\n"
              "      Rb [1234] -> 41\n"
              "      Wb [0207] <- 41\n");
}

TEST(flags_decoded)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x8314); /* sub #1, r4 */
    trace_start(emu);
    step(emu, 1);
    CHECK_STR(trace_text(emu),
              "c000: 8314             sub   #0x0001, r4\n"
              "      SR 0000 -> 0004 [N]\n"
              "      R4 0000 -> ffff\n");
}

TEST(call_and_ret)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x12B0, 0xC010); /* call #0xc010 */
    PROGRAM(0xC010, 0x4130);         /* ret */
    emu->cpu->sp = 0x03FC;

    trace_start(emu);
    step(emu, 2);
    CHECK_STR(trace_text(emu),
              "c000: 12b0 c010        call  #0xc010\n"
              "      W  [03fa] <- c004\n"
              "      SP 03fc -> 03fa\n"
              "c010: 4130             mov   @sp+, pc\n"
              "      R  [03fa] -> c004\n"
              "      SP 03fa -> 03fc\n");
}

TEST(interrupt_entry)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0xD232, /* eint */
            0x3FFF);        /* jmp $ */
    poke(VECTOR_TABLE + 2 * 2, 0xC010);

    trace_start(emu);
    step(emu, 1);
    cpu_set_irq(emu, 2, true);
    step(emu, 1);
    CHECK_STR(trace_text(emu),
              "c000: d232             bis   #0x0008, sr\n"
              "      SR 0000 -> 0008 [GIE]\n"
              "c002: 3fff             jmp   0xc002\n"
              "*** interrupt vector 0xffe4\n"
              "      W  [03fe] <- c002\n"
              "      W  [03fc] <- 0008\n"
              "      R  [ffe4] -> c010\n"
              "      PC c002 -> c010\n"
              "      SP 0400 -> 03fc\n"
              "      SR 0008 -> 0000\n");
}

TEST(sleeping_steps_not_traced)
{
    Emulator *emu = emu_new();

    emu->cpu->sr = SR_CPUOFF;
    trace_start(emu);
    step(emu, 3);
    CHECK_STR(trace_text(emu), "");
}

TEST(off_by_default)
{
    Emulator *emu = emu_new();

    CHECK(!emu->trace);
    PROGRAM(0xC000, 0x40B2, 0x5A80, 0x0120); /* mov #0x5a80, &0x0120 */
    step(emu, 1);
    CHECK_EQ(emu->tracer->count, 0);
}

int main(int argc, char **argv)
{
    static const Test tests[] = {
        T(loads_stores_and_registers), T(flags_decoded),  T(call_and_ret), T(interrupt_entry),
        T(sleeping_steps_not_traced),  T(off_by_default),
    };
    return run_tests(tests, sizeof tests / sizeof *tests, argc, argv);
}
