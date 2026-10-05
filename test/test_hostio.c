/*
 * Host I/O for newlib (hostio/hostio.h): the CIO breakpoint and the
 * syscalls at 0x0180 + N.
 */
#include <string.h>

#include "harness.h"
#include "hostio/hostio.h"
#include "loader/symbols.h"

#define CIOBUF 0x0200
#define CIO_HOOK 0xC002 /* the "cio" label of CIO_PROGRAM */

/* Calls the CIO hook and exits with the result, the first response parameter. */
static const char CIO_PROGRAM[] = "        jmp main\n"
                                  "cio:    nop\n"
                                  "        ret\n"
                                  "main:   call #cio\n"
                                  "        mov &0x0202, r12\n"
                                  "        call #0x0181\n";

/* An emulator with the CIO symbols, CIO_PROGRAM loaded, and a request in the buffer. */
static Emulator *cio_setup(bool symbols, unsigned cmd, unsigned fd, unsigned count,
                           const char *data)
{
    Emulator *emu = emu_new();
    unsigned len  = strlen(data);

    if (symbols) {
        symbols_add(emu, "C$$IO$$", CIO_HOOK, SYM_FUNC);
        symbols_add(emu, "__CIOBUF__", CIOBUF, SYM_DATA);
        symbols_sort(emu);
    }
    assemble(emu, CIO_PROGRAM);
    emu->mem[CIOBUF]     = len;
    emu->mem[CIOBUF + 1] = len >> 8;
    emu->mem[CIOBUF + 2] = cmd;
    emu->mem[CIOBUF + 3] = fd;
    emu->mem[CIOBUF + 4] = fd >> 8;
    emu->mem[CIOBUF + 5] = count;
    emu->mem[CIOBUF + 6] = count >> 8;
    memcpy(emu->mem + CIOBUF + 11, data, len);
    return emu;
}

TEST(cio_symbols_found)
{
    Emulator *emu = cio_setup(true, CIO_WRITE, 1, 0, "");

    CHECK_EQ(emu->cio_hook, CIO_HOOK);
    CHECK_EQ(emu->cio_buf, CIOBUF);
}

TEST(cio_alternative_buffer_name)
{
    Emulator *emu = emu_new();

    symbols_add(emu, "_CIOBUF_", 0x0300, SYM_DATA);
    symbols_sort(emu);
    emu_reset(emu);
    CHECK_EQ(emu->cio_buf, 0x0300);
    CHECK_EQ(emu->cio_hook, -1);
}

TEST(cio_write_stdout)
{
    Emulator *emu = cio_setup(true, CIO_WRITE, 1, 6, "Hello\n");

    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_STR(host_stdout, "Hello\n");
    CHECK_STR(host_stderr, "");
    CHECK_EQ(emu->exit_code, 6);
    CHECK_EQ(peek(CIOBUF), 0); /* no response data */
}

TEST(cio_write_stderr)
{
    Emulator *emu = cio_setup(true, CIO_WRITE, 2, 4, "oops");

    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_STR(host_stderr, "oops");
    CHECK_STR(host_stdout, "");
    CHECK_EQ(emu->exit_code, 4);
}

TEST(cio_write_bad_fd)
{
    Emulator *emu = cio_setup(true, CIO_WRITE, 5, 2, "no");

    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_STR(host_stdout, "");
    CHECK_EQ(emu->exit_code, 0xFFFF);
}

TEST(cio_read_line)
{
    Emulator *emu = cio_setup(true, CIO_READ, 0, 64, "");

    uart_input = "line\nmore";
    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 5);
    CHECK_EQ(peek(CIOBUF), 5); /* response data length */
    CHECK(memcmp(emu->mem + CIOBUF + 10, "line\n", 5) == 0);
    CHECK_STR(uart_input, "more");
}

TEST(cio_read_eof)
{
    Emulator *emu = cio_setup(true, CIO_READ, 0, 64, "");

    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 0);
}

TEST(cio_close_console)
{
    Emulator *emu = cio_setup(true, CIO_CLOSE, 1, 0, "");

    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 0);
}

TEST(cio_files_unsupported)
{
    Emulator *emu = cio_setup(true, CIO_UNLINK, 0, 0, "file.txt");

    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 0xFFFF);
}

/* Without the symbols, the hook is an ordinary nop; ret. */
TEST(cio_without_symbols)
{
    Emulator *emu = cio_setup(false, CIO_WRITE, 1, 6, "Hello\n");

    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_STR(host_stdout, "");
    CHECK_EQ(emu->exit_code, 0x01F3); /* the untouched request: command, fd */
}

TEST(cio_trace)
{
    Emulator *emu = cio_setup(true, CIO_WRITE, 1, 2, "hi");

    trace_start(emu);
    emu_run(emu, 1000);
    const char *t = trace_text(emu);
    CHECK(strstr(t, "*** CIO write(1, 2) = 2\n") != NULL);
    CHECK(strstr(t, "*** syscall exit(0x0002, ") != NULL);
}

TEST(syscall_exit)
{
    Emulator *emu = emu_new();

    assemble(emu, "        mov #42, r12\n"
                  "        call #0x0181\n");
    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 42);
}

/* A tail call by br, as an optimizing compiler may emit for exit(). */
TEST(syscall_exit_by_branch)
{
    Emulator *emu = emu_new();

    assemble(emu, "        mov #7, r12\n"
                  "        br #0x0181\n");
    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 7);
}

TEST(syscall_returns_to_caller)
{
    Emulator *emu = emu_new();
    Cpu *cpu      = emu->cpu;

    assemble(emu, "        mov #9, r12\n"     /* close(9) */
                  "        call #0x0183\n"
                  "        jmp $\n");
    step(emu, 1);
    uint16_t sp       = cpu->sp;
    uint64_t cycles   = cpu->cycles;
    step(emu, 2); /* the call, then the syscall */
    CHECK_EQ(cpu->pc, 0xC008);
    CHECK_EQ(cpu->sp, sp);
    CHECK_EQ(cpu->r12, 0xFFFF);
    CHECK_EQ(cpu->cycles - cycles, 5 + 3); /* call #imm, then as ret */
}

TEST(syscall_write)
{
    Emulator *emu = emu_new();

    assemble(emu, "        .data\n"
                  "msg:    .byte 111, 107, 10\n"
                  "        .text\n"
                  "        mov #1, r12\n"
                  "        mov #msg, r13\n"
                  "        mov #3, r14\n"
                  "        call #0x0185\n"
                  "        call #0x0181\n");
    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_STR(host_stdout, "ok\n");
    CHECK_EQ(emu->exit_code, 3);
}

TEST(syscall_read)
{
    Emulator *emu = emu_new();

    uart_input = "abc\ndef";
    assemble(emu, "        mov #0, r12\n"
                  "        mov #0x0300, r13\n"
                  "        mov #16, r14\n"
                  "        call #0x0184\n"
                  "        call #0x0181\n");
    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 4);
    CHECK(memcmp(emu->mem + 0x0300, "abc\n", 4) == 0);
}

TEST(syscall_read_bad_fd)
{
    Emulator *emu = emu_new();

    uart_input = "abc";
    assemble(emu, "        mov #3, r12\n"
                  "        mov #0x0300, r13\n"
                  "        mov #16, r14\n"
                  "        call #0x0184\n"
                  "        call #0x0181\n");
    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 0xFFFF);
    CHECK_EQ(emu->mem[0x0066], 'a'); /* still waiting in UCA0RXBUF */
    CHECK_STR(uart_input, "bc");
}

/* abort() is kill(getpid(), SIGABRT): the status a host shell would show. */
TEST(syscall_kill)
{
    Emulator *emu = emu_new();

    assemble(emu, "        mov #42, r12\n"
                  "        mov #6, r13\n"
                  "        call #0x0189\n");
    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_EQ(emu->exit_code, 134);
}

TEST(syscall_files_unsupported)
{
    Emulator *emu = emu_new();

    assemble(emu, "        call #0x0182\n" /* open */
                  "        mov r12, r4\n"
                  "        call #0x0186\n" /* lseek */
                  "        mov r12, r5\n"
                  "        call #0x018a\n" /* fstat */
                  "        mov r12, r6\n"
                  "        call #0x01bf\n" /* unknown */
                  "        mov r12, r7\n"
                  "        mov #0, r12\n"
                  "        call #0x0181\n");
    CHECK_EQ(emu_run(emu, 1000), EMU_PROGRAM);
    CHECK_EQ(emu->cpu->r4, 0xFFFF);
    CHECK_EQ(emu->cpu->r5, 0xFFFF);
    CHECK_EQ(emu->cpu->r6, 0xFFFF);
    CHECK_EQ(emu->cpu->r7, 0xFFFF);
}

int main(int argc, char **argv)
{
    static const Test tests[] = {
        T(cio_symbols_found),     T(cio_alternative_buffer_name),
        T(cio_write_stdout),      T(cio_write_stderr),
        T(cio_write_bad_fd),      T(cio_read_line),
        T(cio_read_eof),          T(cio_close_console),
        T(cio_files_unsupported), T(cio_without_symbols),
        T(cio_trace),             T(syscall_exit),
        T(syscall_exit_by_branch), T(syscall_returns_to_caller),
        T(syscall_write),         T(syscall_read),
        T(syscall_read_bad_fd),   T(syscall_kill),
        T(syscall_files_unsupported),
    };
    return run_tests(tests, sizeof tests / sizeof *tests, argc, argv);
}
