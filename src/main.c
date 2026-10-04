/*
 * mspsim: command-line front-end.
 *
 * UART output goes to stdout. Diagnostics go to stderr in batch mode;
 * in the interactive debugger everything goes to stdout.
 */
#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "cpu/registers.h"
#include "debugger/debugger.h"
#include "emulator.h"
#include "io.h"
#include "linenoise.h"
#include "loader/loader.h"
#include "utilities.h"

#define CTRL_RBRACKET 0x1D /* returns to the debugger */

/* Exit statuses, besides the value written to the stop register. */
enum {
    EXIT_LOAD       = 1,
    EXIT_USAGE      = 2,
    EXIT_MAX_CYCLES = 124,
    EXIT_SLEEP      = 125,
    EXIT_INTERRUPT  = 130, /* 128 + SIGINT */
    EXIT_ILLEGAL    = 132, /* 128 + SIGILL */
};

static Emulator *emu;
static uint64_t max_cycles;
static bool quiet;        /* no banner or diagnostics */
static bool interactive;  /* the debugger prompt is active */
static bool debugger_key; /* Ctrl-] was pressed during the run */
static bool stdin_eof;

static struct termios saved_tty;
static bool tty_raw, tty_atexit;

void print_console(Emulator *e, const char *buf)
{
    (void)e;
    if (interactive)
        fputs(buf, stdout);
    else if (!quiet)
        fputs(buf, stderr);
}

void uart_tx(Emulator *e, uint8_t byte)
{
    (void)e;
    putchar(byte);
    fflush(stdout);
}

int uart_rx(Emulator *e)
{
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
    unsigned char c;

    if (stdin_eof)
        return UART_EOF;
    if (poll(&pfd, 1, 0) <= 0)
        return -1;
    if (read(STDIN_FILENO, &c, 1) != 1) {
        stdin_eof = true;
        return UART_EOF;
    }
    if (tty_raw && c == CTRL_RBRACKET) {
        debugger_key     = true;
        e->break_request = 1;
        return -1;
    }
    return c;
}

static void handle_sigint(int sig)
{
    (void)sig;
    if (emu != NULL)
        emu->break_request = 1;
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
        if (!tty_atexit)
            atexit(tty_restore);
        tty_atexit = true;
    }
}

static StopReason run(void)
{
    StopReason reason;

    debugger_key       = false;
    emu->break_request = 0; /* drop a Ctrl-C typed at the prompt */
    tty_make_raw();
    reason = emu_run(emu, max_cycles);
    tty_restore();
    return reason;
}

static int exit_status(StopReason reason)
{
    switch (reason) {
    case EMU_PROGRAM:
        return emu->exit_code & 0xFF;
    case EMU_MAX_CYCLES:
        return EXIT_MAX_CYCLES;
    case EMU_SLEEP:
        return EXIT_SLEEP;
    case EMU_ILLEGAL:
        return EXIT_ILLEGAL;
    case EMU_INTERRUPT:
        return EXIT_INTERRUPT;
    default:
        return 0;
    }
}

static int debug_loop(bool resumed)
{
    char last[256] = "";

    interactive          = true;
    emu->debugger->color = isatty(STDOUT_FILENO);
    linenoiseHistorySetMaxLen(200);

    if (resumed)
        report_stop(emu);
    else
        exec_cmd(emu, "regs");

    for (;;) {
        errno      = 0;
        char *line = linenoise("(mspsim) ");
        if (line == NULL) {
            if (errno == EAGAIN) /* Ctrl-C */
                continue;
            break; /* end of input */
        }

        /* An empty line repeats the previous command. */
        if (line[0]) {
            linenoiseHistoryAdd(line);
            snprintf(last, sizeof last, "%s", line);
        }
        linenoiseFree(line);

        DebugAction action = exec_cmd(emu, last);
        if (action == DBG_QUIT)
            break;
        if (action == DBG_RUN) {
            run();
            report_stop(emu);
        }
    }
    return 0;
}

static void usage(FILE *f)
{
    fprintf(f,
            "Usage: mspsim [options] firmware\n"
            "  -b, --binary ADDR     raw binary loaded at ADDR (default: ELF or Intel HEX)\n"
            "  -g, --debug           start paused in the interactive debugger\n"
            "  -n, --max-cycles N    stop after N cycles\n"
            "  -t, --trace           trace executed instructions, register changes, "
            "loads/stores\n"
            "  -o, --trace-file F    write trace to F instead of stderr (implies -t)\n"
            "  -q, --quiet           no banner/diagnostics, only UART output\n"
            "  -h, --help            show this help\n"
            "\n"
            "Ctrl-] returns to the debugger. Exit status: the value written to the stop\n"
            "register 0x01FE; 124 cycle limit, 125 CPU off with no wake-up, 130 interrupted,\n"
            "132 illegal instruction, 1 load error, 2 usage error.\n");
}

static bool parse_number(const char *s, unsigned long long max, unsigned long long *out)
{
    char *end;

    errno = 0;
    *out  = strtoull(s, &end, 0);
    return errno == 0 && end != s && *end == 0 && *out <= max;
}

int main(int argc, char *argv[])
{
    static const struct option options[] = {
        { "binary", required_argument, NULL, 'b' },
        { "debug", no_argument, NULL, 'g' },
        { "max-cycles", required_argument, NULL, 'n' },
        { "trace", no_argument, NULL, 't' },
        { "trace-file", required_argument, NULL, 'o' },
        { "quiet", no_argument, NULL, 'q' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };
    unsigned long long addr = 0, number;
    bool binary = false, debug = false, trace = false;
    const char *trace_path = NULL;
    int c, status;

    while ((c = getopt_long(argc, argv, "b:gn:to:qh", options, NULL)) != -1) {
        switch (c) {
        case 'b':
            if (!parse_number(optarg, 0xFFFF, &addr)) {
                fprintf(stderr, "mspsim: bad address '%s'\n", optarg);
                return EXIT_USAGE;
            }
            binary = true;
            break;
        case 'g':
            debug = true;
            break;
        case 'n':
            if (!parse_number(optarg, UINT64_MAX, &number) || number == 0) {
                fprintf(stderr, "mspsim: bad cycle count '%s'\n", optarg);
                return EXIT_USAGE;
            }
            max_cycles = number;
            break;
        case 't':
            trace = true;
            break;
        case 'o':
            trace_path = optarg;
            trace      = true;
            break;
        case 'q':
            quiet = true;
            break;
        case 'h':
            usage(stdout);
            return 0;
        default:
            usage(stderr);
            return EXIT_USAGE;
        }
    }
    if (optind != argc - 1) {
        usage(stderr);
        return EXIT_USAGE;
    }
    const char *path = argv[optind];

    emu        = emu_create();
    emu->trace = trace;
    if (trace_path != NULL && (emu->trace_file = fopen(trace_path, "w")) == NULL) {
        fprintf(stderr, "mspsim: %s: %s\n", trace_path, strerror(errno));
        emu_destroy(emu);
        return EXIT_LOAD;
    }

    char err[256];
    long size = binary ? load_binary(emu, path, addr, err, sizeof err)
                       : load_firmware(emu, path, err, sizeof err);
    if (size < 0) {
        fprintf(stderr, "mspsim: %s: %s\n", path, err);
        status = EXIT_LOAD;
        goto done;
    }
    emu_reset(emu);
    if (!quiet)
        emu_printf(emu, "Loaded %s: %ld bytes, %d symbols, start at 0x%04x\n", path, size,
                   emu->nsymbols, emu->cpu->pc);

    signal(SIGINT, handle_sigint);

    if (debug) {
        status = debug_loop(false);
    } else {
        StopReason reason = run();

        if (reason == EMU_INTERRUPT && debugger_key) {
            status = debug_loop(true);
        } else {
            if (!quiet) {
                if (reason == EMU_PROGRAM)
                    emu_printf(emu, "\n[Exit code %u after %llu cycles]\n", emu->exit_code,
                               (unsigned long long)emu->cpu->cycles);
                else
                    emu_printf(emu, "\n[%s at 0x%04X after %llu cycles]\n", emu_stop_reason(reason),
                               emu->cpu->pc, (unsigned long long)emu->cpu->cycles);
            }
            status = exit_status(reason);
        }
    }

done:
    signal(SIGINT, SIG_DFL);
    if (emu->trace_file != NULL)
        fclose(emu->trace_file);
    emu_destroy(emu);
    emu = NULL;
    return status;
}
