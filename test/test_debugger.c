#include <string.h>

#include "debug/debugger.h"
#include "harness.h"

TEST(actions)
{
    Emulator *emu = emu_new();

    CHECK_EQ(exec_cmd(emu, "quit"), DBG_QUIT);
    CHECK_EQ(exec_cmd(emu, "run"), DBG_RUN);
    CHECK_EQ(exec_cmd(emu, "c"), DBG_RUN);
    CHECK_EQ(exec_cmd(emu, "regs"), DBG_STAY);
}

TEST(empty_line)
{
    Emulator *emu = emu_new();

    CHECK_EQ(exec_cmd(emu, ""), DBG_STAY);
    CHECK_EQ(exec_cmd(emu, "   "), DBG_STAY);
    CHECK_EQ(console_text[0], 0);
}

TEST(invalid_command)
{
    Emulator *emu = emu_new();

    exec_cmd(emu, "bogus");
    CHECK(strstr(console_text, "Invalid command") != NULL);
}

TEST(step_n)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x4314, /* mov #1, r4 */
            0x4325,         /* mov #2, r5 */
            0x3FFF);        /* jmp $ */
    exec_cmd(emu, "step 2");
    CHECK_EQ(emu->cpu->r5, 2);
    CHECK_EQ(emu->cpu->pc, 0xC004);
    CHECK(strstr(console_text, "jmp") != NULL);
}

TEST(step_stops_at_stop_register)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x40B2, 0x0007, 0x01FE, /* mov #7, &0x01fe */
            0x4314);                        /* mov #1, r4 */
    exec_cmd(emu, "step 5");
    CHECK_EQ(emu->cpu->r4, 0);
    CHECK(strstr(console_text, "exit code 7") != NULL);
}

TEST(set_register_and_memory)
{
    Emulator *emu = emu_new();

    exec_cmd(emu, "set r5 1234");
    exec_cmd(emu, "set sr 0100");
    exec_cmd(emu, "set 0200 BEEF");
    CHECK_EQ(emu->cpu->r5, 0x1234);
    CHECK_EQ(emu->cpu->sr, 0x0100);
    CHECK_EQ(peek(0x0200), 0xBEEF);
}

TEST(trace_toggle)
{
    Emulator *emu = emu_new();

    exec_cmd(emu, "trace on");
    CHECK(emu->trace);
    exec_cmd(emu, "trace off");
    CHECK(!emu->trace);
}

TEST(breakpoints_listed)
{
    Emulator *emu = emu_new();

    exec_cmd(emu, "break C010");
    exec_cmd(emu, "bps");
    CHECK(strstr(console_text, "0xC010") != NULL);
    CHECK_EQ(breakpoint_at(emu, 0xC010), 0);
    CHECK_EQ(breakpoint_at(emu, 0xC012), -1);
}

TEST(disassemble_illegal)
{
    Emulator *emu = emu_new();

    PROGRAM(0xC000, 0x0000);
    exec_cmd(emu, "dis 1");
    CHECK(strstr(console_text, ".word 0x0000") != NULL);
    CHECK_EQ(emu->stop, EMU_RUNNING);
}

int main(int argc, char **argv)
{
    static const Test tests[] = {
        T(actions),
        T(empty_line),
        T(invalid_command),
        T(step_n),
        T(step_stops_at_stop_register),
        T(set_register_and_memory),
        T(trace_toggle),
        T(breakpoints_listed),
        T(disassemble_illegal),
    };
    return run_tests(tests, sizeof tests / sizeof *tests, argc, argv);
}
