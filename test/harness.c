#include "harness.h"

#include <stdlib.h>
#include <string.h>

#include "debugger/debugger.h"
#include "io.h"
#include "memory/memspace.h"
#include "utilities.h"

int test_failed;
char console_text[4096];

static Emulator *current;

void print_console(Emulator *emu, const char *buf)
{
    (void)emu;
    str_append(console_text, sizeof console_text, buf);
}

Emulator *emu_new(void)
{
    Emulator *emu = calloc(1, sizeof(Emulator));
    emu->cpu      = calloc(1, sizeof(Cpu));
    emu->debugger = calloc(1, sizeof(Debugger));
    setup_debugger(emu);
    initialize_msp_memspace();
    initialize_msp_registers(emu);
    current = emu;
    return emu;
}

static void emu_free(void)
{
    if (current == NULL)
        return;
    uninitialize_msp_memspace();
    free(current->cpu);
    free(current->debugger);
    free(current);
    current = NULL;
}

void poke(uint16_t addr, uint16_t value)
{
    *get_addr_ptr(addr) = value;
}

uint16_t peek(uint16_t addr)
{
    return *get_addr_ptr(addr);
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
