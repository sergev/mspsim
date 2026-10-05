/*
 * Standard I/O through newlib's host calls (msp430-elf-gcc -msim): printf
 * and fputs go through the CIO breakpoint, fgets through the read syscall,
 * and exit through the exit syscall.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    char line[32];
    int lines = 0;

    printf("Hello from newlib: %d %ld %x %s\n", -5, 70000L, 0xbeef, "ok");
    fputs("to stderr\n", stderr);
    while (fgets(line, sizeof line, stdin) != NULL) {
        line[strcspn(line, "\n")] = 0;
        printf("[%s] %u\n", line, (unsigned)strlen(line));
        lines++;
    }
    printf("%d lines\n", lines);
    fflush(stdout);
    exit(lines == 2 ? 0 : 1);
}
