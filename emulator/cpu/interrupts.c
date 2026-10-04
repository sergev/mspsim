#include "cpu/interrupts.h"

#include <stdbool.h>
#include <stdint.h>

#include "cpu/registers.h"
#include "memory/memory.h"

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

int handle_interrupts(Emulator *emu)
{
    Cpu *cpu         = emu->cpu;
    uint16_t pending = cpu->irq_pending;
    int irq;

    if (!(cpu->sr & SR_GIE))
        pending &= 1u << NMI_IRQ;
    if (pending == 0)
        return -1;

    for (irq = NMI_IRQ; !(pending & (1u << irq)); irq--)
        ;
    if (irq == NMI_IRQ)
        cpu->irq_pending &= ~(1u << NMI_IRQ); /* edge-triggered */

    cpu->sp -= 2;
    mem_write(emu, cpu->sp, cpu->pc, 2);
    cpu->sp -= 2;
    mem_write(emu, cpu->sp, cpu->sr, 2);

    cpu->pc = mem_read(emu, VECTOR_TABLE + 2 * irq, 2, ACC_DATA);

    /* SR is cleared except SCG0 (SLAU144, 2.2.3). */
    cpu->sr &= SR_SCG0;
    return irq;
}
