/*
 * Compiler-generated arithmetic, calls and data initialization.
 */
#include "sim.h"

int initialized = 42; /* .data, copied from flash by the C runtime */
int zeroed;           /* .bss */

static int fib(int n)
{
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

static void sort(int *a, int n)
{
    for (int i = 0; i < n; i++)
        for (int j = n - 1; j > i; j--)
            if (a[j] < a[j - 1]) {
                int t    = a[j];
                a[j]     = a[j - 1];
                a[j - 1] = t;
            }
}

int main(void)
{
    volatile int a = 1234, b = 56, m = -45, d = 7;
    volatile unsigned u = 0x8000;
    volatile int s      = -32768;
    volatile long big   = 100000;
    int list[]          = { 5, -3, 9, 0, 2 };

    sim_show("initialized", initialized);
    sim_show("zeroed", zeroed);
    sim_show("a * b", (long)a * b);
    sim_show("a * b / d", (long)a * b / d);
    sim_show("a * b % 1000", (long)a * b % 1000);
    sim_show("m / d", m / d);
    sim_show("m % d", m % d);
    sim_show("a << 3", (unsigned)(a << 3));
    sim_show("u >> 15", u >> 15);
    sim_show("s >> 15", s >> 15);
    sim_show("big * 3", big * 3);
    sim_show("big * 10 / 3", big * 10 / 3);
    sim_show("fib(15)", fib(15));

    sort(list, 5);
    sim_puts("sorted =");
    for (int i = 0; i < 5; i++) {
        sim_putc(' ');
        sim_putd(list[i]);
    }
    sim_putc('\n');

    sim_puts("hex = ");
    sim_putx(0xBEEF);
    sim_putc('\n');
    sim_exit(0);
}
