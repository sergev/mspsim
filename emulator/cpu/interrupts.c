#include "cpu/interrupts.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cpu/registers.h"
#include "utilities.h"

void cpu_set_irq(Emulator *emu, unsigned irq, bool level)
{
    Cpu *cpu = emu->cpu;

    if (irq >= RESET_IRQ)
        return;
    if (level)
        cpu->irq_pending |= 1u << irq;
    else
        cpu->irq_pending &= ~(1u << irq);
}

bool handle_interrupts(Emulator *emu)
{
    Cpu *cpu         = emu->cpu;
    uint16_t pending = cpu->irq_pending;
    int irq;

    if (!cpu->sr.GIE)
        pending &= 1u << NMI_IRQ;
    if (pending == 0)
        return false;

    for (irq = NMI_IRQ; !(pending & (1u << irq)); irq--)
        ;
    if (irq == NMI_IRQ)
        cpu->irq_pending &= ~(1u << NMI_IRQ); /* edge-triggered */

    cpu->sp -= 2;
    *get_stack_ptr(emu) = cpu->pc;
    cpu->sp -= 2;
    *get_stack_ptr(emu) = sr_to_value(emu);

    cpu->pc = *get_addr_ptr(VECTOR_TABLE + 2 * irq);

    /* SR is cleared except SCG0 (SLAU144, 2.2.3). */
    uint8_t scg0 = cpu->sr.SCG0;
    memset(&cpu->sr, 0, sizeof cpu->sr);
    cpu->sr.SCG0 = scg0;
    return true;
}
