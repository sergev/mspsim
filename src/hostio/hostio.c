#include "hostio/hostio.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cpu/registers.h"
#include "debug/trace.h"
#include "io.h"
#include "loader/symbols.h"
#include "mem/memory.h"
#include "uart/uart.h"

/*
 * CIO buffer layout, as TI's run-time library and libgloss use it.
 * Request: data length (2 bytes), command (1), parameters (8), data.
 * Response: data length (2), parameters (8), data.
 */
#define CIO_REQ_PARMS 3
#define CIO_REQ_DATA  11
#define CIO_RSP_PARMS 2
#define CIO_RSP_DATA  10
#define CIO_NPARMS    8

void hostio_reset(Emulator *emu)
{
    uint16_t addr;

    emu->cio_hook = symbol_lookup(emu, "C$$IO$$", &addr) ? addr : -1;
    if (symbol_lookup(emu, "__CIOBUF__", &addr) || symbol_lookup(emu, "_CIOBUF_", &addr))
        emu->cio_buf = addr;
    else
        emu->cio_buf = -1;
}

/* Copy between emulated memory and the host, wrapping at 64 KB. */
static void copy_in(Emulator *emu, uint8_t *dst, uint16_t src, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        dst[i] = emu->mem[(uint16_t)(src + i)];
}

static void copy_out(Emulator *emu, uint16_t dst, const uint8_t *src, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        emu->mem[(uint16_t)(dst + i)] = src[i];
}

/* write(fd, data, n) on the console; -1 for any other fd. */
static int console_write(Emulator *emu, unsigned fd, const uint8_t *data, unsigned n)
{
    if (fd != 1 && fd != 2)
        return -1;
    return host_write(emu, fd, data, n);
}

/*
 * read(fd, data, n) from the console; -1 for any other fd. The UART polls the
 * same stdin, so a byte it has already taken comes first.
 */
static int console_read(Emulator *emu, unsigned fd, uint8_t *data, unsigned n)
{
    int first, rest;

    if (fd != 0)
        return -1;
    if (n == 0)
        return 0;
    if ((first = uart_take_input(emu)) < 0)
        return host_read(emu, data, n);
    data[0] = first;
    if (first == '\n' || n == 1)
        return 1;
    rest = host_read(emu, data + 1, n - 1);
    return rest > 0 ? 1 + rest : 1;
}

static void trace_cio(Emulator *emu, const char *name, int fd, int count, int rv)
{
    char text[80];

    snprintf(text, sizeof text, "CIO %s(%d, %d) = %d", name, fd, count, rv);
    trace_host(emu, text);
}

void hostio_cio(Emulator *emu)
{
    if (emu->cio_buf < 0)
        return;

    uint16_t buf = emu->cio_buf;
    uint8_t parms[CIO_NPARMS];
    unsigned length = emu->mem[buf] | emu->mem[(uint16_t)(buf + 1)] << 8;
    unsigned cmd    = emu->mem[(uint16_t)(buf + 2)];
    uint8_t *data   = malloc(length > 0 ? length : 1);
    unsigned fd, count, out_length = 0;
    const char *name;
    int rv = -1;

    if (emu->trace)
        trace_snapshot(emu);
    copy_in(emu, parms, buf + CIO_REQ_PARMS, CIO_NPARMS);
    copy_in(emu, data, buf + CIO_REQ_DATA, length);
    fd    = parms[0] | parms[1] << 8;
    count = parms[2] | parms[3] << 8;

    switch (cmd) {
    case CIO_WRITE:
        name = "write";
        rv   = console_write(emu, fd, data, count < length ? count : length);
        break;
    case CIO_READ:
        name = "read";
        data = realloc(data, count > 0 ? count : 1);
        rv   = console_read(emu, fd, data, count);
        if (rv > 0)
            out_length = rv;
        break;
    case CIO_CLOSE:
        name = "close";
        rv   = fd <= 2 ? 0 : -1;
        break;
    default: /* files, the environment and the clocks are not served */
        name  = "unsupported";
        fd    = cmd;
        count = 0;
        break;
    }

    /* The result is the first parameter; a 32-bit one (lseek, the clocks) fills four. */
    for (int i = 0; i < CIO_NPARMS; i++)
        parms[i] = i < 4 ? (uint32_t)rv >> (8 * i) : 0;
    emu->mem[buf]                  = out_length;
    emu->mem[(uint16_t)(buf + 1)]  = out_length >> 8;
    copy_out(emu, buf + CIO_RSP_PARMS, parms, CIO_NPARMS);
    copy_out(emu, buf + CIO_RSP_DATA, data, out_length);
    free(data);

    if (emu->trace)
        trace_cio(emu, name, fd, count, rv);
}

/* The data of read(fd, buf, n) or write(fd, buf, n), in emulated memory. */
static int syscall_rw(Emulator *emu, bool is_read, unsigned fd, uint16_t addr, unsigned n)
{
    uint8_t *data = malloc(n > 0 ? n : 1);
    int rv;

    if (is_read) {
        rv = console_read(emu, fd, data, n);
        if (rv > 0)
            copy_out(emu, addr, data, rv);
    } else {
        copy_in(emu, data, addr, n);
        rv = console_write(emu, fd, data, n);
    }
    free(data);
    return rv;
}

static void stop(Emulator *emu, uint16_t code)
{
    emu->stop      = EMU_PROGRAM;
    emu->exit_code = code;
}

unsigned hostio_syscall(Emulator *emu)
{
    Cpu *cpu   = emu->cpu;
    unsigned n = cpu->pc - SYSCALL_BASE;
    uint16_t a = cpu->r12, b = cpu->r13, c = cpu->r14;
    char unknown[8];
    const char *name;
    int rv = -1;

    if (emu->trace)
        trace_snapshot(emu);

    switch (n) {
    case SYS_EXIT:
        name = "exit";
        stop(emu, a);
        rv = 0;
        break;
    case SYS_KILL: /* as by a signal: abort() is kill(getpid(), SIGABRT) */
        name = "kill";
        stop(emu, 128 + b);
        rv = 0;
        break;
    case SYS_READ:
        name = "read";
        rv   = syscall_rw(emu, true, a, b, c);
        break;
    case SYS_WRITE:
        name = "write";
        rv   = syscall_rw(emu, false, a, b, c);
        break;
    case SYS_CLOSE:
        name = "close";
        rv   = a <= 2 ? 0 : -1;
        break;
    case SYS_OPEN:
        name = "open";
        break;
    case SYS_LSEEK:
        name = "lseek";
        break;
    case SYS_FSTAT:
        name = "fstat";
        break;
    default:
        snprintf(unknown, sizeof unknown, "#%u", n);
        name = unknown;
        break;
    }

    /* The result in R12, then back to the caller. */
    cpu->r12 = rv;
    cpu->pc  = mem_read(emu, cpu->sp, 2, ACC_DATA);
    cpu->sp += 2;

    if (emu->trace) {
        char text[80];

        snprintf(text, sizeof text, "syscall %s(0x%04x, 0x%04x, 0x%04x) = %d", name, a, b, c, rv);
        trace_host(emu, text);
    }
    return 3; /* RET */
}
