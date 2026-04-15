#include "timer.h"
#include "io.h"
#include "irq.h"
#include <stdio.h>

#define PIT_CHANNEL0 0x40
#define PIT_CMD 0x43
#define PIT_BASE_FREQ 1193182

static uint32_t g_Ticks = 0;

static void timer_handler(Registers *regs) {
	g_Ticks++;
	// print a dot every 100 ticks so it's visible but not spammy
	if (g_Ticks % 100 == 0)
		printf(".");
}

void i686_Timer_Initialize(uint32_t frequency) {
	// register handler for IRQ0 (timer)
	i686_IRQ_RegisterHandler(0, timer_handler);

	// configure PIT channel 0
	uint32_t divisor = PIT_BASE_FREQ / frequency;
	i686_outb(PIT_CMD, 0x36); // channel 0, lobyte/hibyte, rate generator
	i686_outb(PIT_CHANNEL0, divisor & 0xFF);
	i686_outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

	// unmask IRQ0
	i686_IRQ_Unmask(0);
}
