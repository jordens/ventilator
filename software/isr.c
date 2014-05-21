#include <generated/csr.h>
#include <irq.h>
#include <uart.h>
#include "ventilator.h"

void isr(void);
void __attribute__((used)) isr(void)
{
	unsigned int irqs;

	irqs = irq_pending() & irq_getmask();

	if(irqs & (1 << VENTILATOR_INTERRUPT))
		ventilator_isr();
	if(irqs & (1 << UART_INTERRUPT))
		uart_isr();
}
