/*
 * Minimal unit test harness.
 *
 * Each test file defines TEST() functions and a main() that passes
 * them to run_tests(). Run a single case with: ./test_x case_name
 */
#ifndef _HARNESS_H_
#define _HARNESS_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* CPU state and interrupt API, used by every test. */
#include "cpu/interrupts.h"
#include "cpu/registers.h"
#include "emulator.h"

typedef struct {
    const char *name;
    void (*fn)(void);
} Test;

#define TEST(name) static void name(void)
#define T(name)    { #name, name }

int run_tests(const Test *tests, size_t n, int argc, char **argv);

extern int test_failed;

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            test_failed = 1;                                                  \
        }                                                                     \
    } while (0)

#define CHECK_EQ(actual, expected)                                                              \
    do {                                                                                        \
        unsigned long long _a = (actual), _e = (expected);                                      \
        if (_a != _e) {                                                                         \
            printf("  %s:%d: %s == 0x%llx, expected 0x%llx\n", __FILE__, __LINE__, #actual, _a, \
                   _e);                                                                         \
            test_failed = 1;                                                                    \
        }                                                                                       \
    } while (0)

/* Fresh emulator with zeroed registers and memory; freed by run_tests(). */
Emulator *emu_new(void);

/* Word access to emulated memory. */
void poke(uint16_t addr, uint16_t value);
uint16_t peek(uint16_t addr);

/* Store consecutive words starting at addr. */
#define PROGRAM(addr, ...)                                \
    poke_words((addr), (const uint16_t[]){ __VA_ARGS__ }, \
               sizeof((const uint16_t[]){ __VA_ARGS__ }) / sizeof(uint16_t))
void poke_words(uint16_t addr, const uint16_t *words, size_t n);

void step(Emulator *emu, int n);

/* Text sent to print_console() since the test started. */
extern char console_text[4096];

#endif
