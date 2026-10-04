#include "harness.h"

#include <stdlib.h>
#include <string.h>

#include "asm.h"
#include "io.h"
#include "memory/memory.h"
#include "utilities.h"

int test_failed;
char console_text[4096];
char uart_output[4096];
const char *uart_input;

static Emulator *current;

void print_console(Emulator *emu, const char *buf)
{
    (void)emu;
    str_append(console_text, sizeof console_text, buf);
}

void uart_tx(Emulator *emu, uint8_t byte)
{
    char s[2] = { (char)byte, 0 };

    (void)emu;
    str_append(uart_output, sizeof uart_output, s);
}

int uart_rx(Emulator *emu)
{
    (void)emu;
    if (uart_input == NULL || *uart_input == 0)
        return UART_EOF;
    return (unsigned char)*uart_input++;
}

void assemble(Emulator *emu, const char *source)
{
    char err[256];

    if (asm_text(emu, source, err, sizeof err) < 0) {
        printf("  assembler: %s\n", err);
        test_failed = 1;
    }
    emu_reset(emu);
}

void trace_start(Emulator *emu)
{
    emu->trace      = true;
    emu->trace_file = tmpfile();
}

const char *trace_text(Emulator *emu)
{
    static char buf[8192];
    size_t n;

    rewind(emu->trace_file);
    n      = fread(buf, 1, sizeof buf - 1, emu->trace_file);
    buf[n] = 0;
    fclose(emu->trace_file);
    emu->trace_file = NULL;
    emu->trace      = false;
    return buf;
}

Emulator *emu_new(void)
{
    emu_destroy(current); /* a test may make several */
    current = emu_create();
    poke(0xFFFE, 0xC000); /* reset vector */
    emu_reset(current);
    return current;
}

static void emu_free(void)
{
    emu_destroy(current);
    current = NULL;
}

void poke(uint16_t addr, uint16_t value)
{
    mem_write(current, addr, value, 2);
}

uint16_t peek(uint16_t addr)
{
    return mem_read(current, addr, 2, ACC_DATA);
}

void poke_words(uint16_t addr, const uint16_t *words, size_t n)
{
    for (size_t i = 0; i < n; i++)
        poke(addr + 2 * i, words[i]);
}

void step(Emulator *emu, int n)
{
    while (n-- > 0)
        cpu_step(emu);
}

int run_tests(const Test *tests, size_t n, int argc, char **argv)
{
    int failures = 0, ran = 0;

    for (size_t i = 0; i < n; i++) {
        if (argc > 1 && strcmp(argv[1], tests[i].name) != 0)
            continue;
        test_failed     = 0;
        console_text[0] = 0;
        uart_output[0]  = 0;
        uart_input      = NULL;
        tests[i].fn();
        emu_free();
        printf("%s %s\n", test_failed ? "FAIL" : "ok  ", tests[i].name);
        failures += test_failed;
        ran++;
    }
    if (ran == 0) {
        printf("no test named '%s'\n", argv[1]);
        return 1;
    }
    printf("%d of %d failed\n", failures, ran);
    return failures != 0;
}
