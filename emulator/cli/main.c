/*
 * Temporary CLI driver: mspsim firmware.bin [max_steps]
 * Replaced by the real front-end in Step 6.
 */
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "cpu/registers.h"
#include "debugger/debugger.h"
#include "debugger/register_display.h"
#include "emulator.h"
#include "io.h"
#include "utilities.h"

#define CTRL_RBRACKET 0x1D /* returns to the debugger */

static Emulator *sigint_emu;
static struct termios saved_tty;
static bool tty_raw, stdin_eof;

static void handle_sigint(int sig)
{
    (void)sig;
    if (sigint_emu == NULL)
        return;
    sigint_emu->cpu->running         = false;
    sigint_emu->debugger->debug_mode = true;
}

/* Raw stdin while running, so the UART gets each key; Ctrl-C still works. */
static void tty_restore(void)
{
    if (tty_raw) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_tty);
        tty_raw = false;
    }
}

static void tty_make_raw(void)
{
    struct termios t;

    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &saved_tty) < 0)
        return;
    t = saved_tty;
    t.c_iflag &= ~(IXON | ICRNL);
    t.c_lflag &= ~(ICANON | ECHO | IEXTEN);
    t.c_cc[VMIN]  = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) == 0) {
        tty_raw = true;
        atexit(tty_restore);
    }
}

void uart_tx(Emulator *emu, uint8_t byte)
{
    (void)emu;
    putchar(byte);
    fflush(stdout);
}

int uart_rx(Emulator *emu)
{
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
    unsigned char c;

    if (stdin_eof || poll(&pfd, 1, 0) <= 0)
        return -1;
    if (read(STDIN_FILENO, &c, 1) != 1) {
        stdin_eof = true;
        return -1;
    }
    if (tty_raw && c == CTRL_RBRACKET) {
        emu->cpu->running         = false;
        emu->debugger->debug_mode = true;
        return -1;
    }
    return c;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s firmware.bin [max_steps]\n", argv[0]);
        return 2;
    }
    unsigned long long max_steps = (argc > 2) ? strtoull(argv[2], NULL, 0) : 0;

    Emulator *emu = emu_create();
    Cpu *cpu      = emu->cpu;
    Debugger *deb = emu->debugger;

    sigint_emu = emu;
    signal(SIGINT, handle_sigint);

    int status = 1;
    if (load_firmware(emu, argv[1], 0xC000) == 0) {
        cpu->running    = true;
        deb->debug_mode = false;
        tty_make_raw();
        for (unsigned long long n = 0;
             cpu->running && !deb->quit && (max_steps == 0 || n < max_steps); n++)
            cpu_step(emu);
        tty_restore();
        if (emu->stopped) {
            status = emu->exit_code;
        } else {
            display_registers(emu);
            status = 0;
        }
    }

    sigint_emu = NULL;
    emu_destroy(emu);
    return status;
}
