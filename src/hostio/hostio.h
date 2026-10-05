#ifndef _HOSTIO_H_
#define _HOSTIO_H_

#include <stdbool.h>
#include <stdint.h>

#include "emulator.h"

/*
 * Host I/O for newlib's MSP430 board support (libgloss libsim.a, linked by
 * msp430-elf-gcc -msim), which uses two mechanisms:
 *
 * - TI's CIO protocol: the program fills the buffer __CIOBUF__ (or _CIOBUF_)
 *   and calls C$$IO$$, a "nop; ret" on which a debugger is expected to stop
 *   and serve the request. newlib's write() and unlink() use it.
 * - The GDB simulator's syscalls: exit, open, close, read, lseek, kill and
 *   fstat are symbols at 0x0180 + N, and the program calls them there. On a
 *   real chip a fetch from below 0x0200 resets the device, so no working
 *   program executes there otherwise.
 *
 * Only the console is served: reads from fd 0, writes to fds 1 and 2. Every
 * file operation fails with -1.
 */

#define SYSCALL_BASE 0x0180 /* syscall N is a call to SYSCALL_BASE + N */
#define SYSCALL_END  0x01C0

/* SYS_* numbers of libgloss/syscall.h that are served. */
enum {
    SYS_EXIT  = 1,
    SYS_OPEN  = 2,
    SYS_CLOSE = 3,
    SYS_READ  = 4,
    SYS_WRITE = 5,
    SYS_LSEEK = 6,
    SYS_KILL  = 9,
    SYS_FSTAT = 10,
};

/* CIO commands (TI's, as in libgloss/msp430/cio.h). */
enum {
    CIO_OPEN    = 0xF0,
    CIO_CLOSE   = 0xF1,
    CIO_READ    = 0xF2,
    CIO_WRITE   = 0xF3,
    CIO_LSEEK   = 0xF4,
    CIO_UNLINK  = 0xF5,
    CIO_GETENV  = 0xF6,
    CIO_RENAME  = 0xF7,
    CIO_GETTIME = 0xF8,
    CIO_GETCLK  = 0xF9,
};

/* Find C$$IO$$ and the CIO buffer among the firmware's symbols. */
void hostio_reset(Emulator *emu);

/* PC is at C$$IO$$: serve the request in the CIO buffer. The hook itself
 * then executes as usual. */
void hostio_cio(Emulator *emu);

/* PC is in the syscall range: perform the call, then return to the caller as
 * RET would. Returns the cycles taken (those of RET). */
unsigned hostio_syscall(Emulator *emu);

static inline bool hostio_is_syscall(uint16_t pc)
{
    return pc >= SYSCALL_BASE && pc < SYSCALL_END;
}

#endif
