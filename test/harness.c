#include "harness.h"

#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "memory/memory.h"
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
    current = emu_create();
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
