/*
 * Console UART and stop register of the mspsim simulator, for firmware tests.
 */
#ifndef _SIM_H_
#define _SIM_H_

#define SIM_IFG2      (*(volatile unsigned char *)0x0003)
#define SIM_UCA0TXBUF (*(volatile unsigned char *)0x0067)
#define SIM_STOP      (*(volatile unsigned int *)0x01FE)

static inline void sim_putc(char c)
{
    while (!(SIM_IFG2 & 0x02)) /* UCA0TXIFG */
        ;
    SIM_UCA0TXBUF = c;
}

static inline void sim_puts(const char *s)
{
    while (*s)
        sim_putc(*s++);
}

static inline void sim_putd(long v)
{
    char buf[12];
    int n           = 0;
    unsigned long u = v < 0 ? -(unsigned long)v : (unsigned long)v;

    do
        buf[n++] = '0' + u % 10;
    while ((u /= 10) != 0);
    if (v < 0)
        sim_putc('-');
    while (n > 0)
        sim_putc(buf[--n]);
}

static inline void sim_putx(unsigned v)
{
    for (int shift = 12; shift >= 0; shift -= 4)
        sim_putc("0123456789abcdef"[(v >> shift) & 0xF]);
}

/* Print "name = value" for a signed decimal value. */
static inline void sim_show(const char *name, long v)
{
    sim_puts(name);
    sim_puts(" = ");
    sim_putd(v);
    sim_putc('\n');
}

static inline void sim_exit(int code)
{
    SIM_STOP = code;
    for (;;)
        ;
}

#endif
