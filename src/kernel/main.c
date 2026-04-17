#include "memory.h"
#include "pmm.h"
#include "scheduler.h"
#include "stdio.h"
#include <arch/i686/irq.h>
#include <arch/i686/keyboard.h>
#include <arch/i686/timer.h>
#include <boot/bootparams.h>
#include <debug.h>
#include <hal/hal.h>
#include <stdint.h>

extern uint32_t __entry_start;
extern uint32_t __end;
extern void _init();

// --- demo tasks ---

void taskA() {
	for (;;) {
		printf("A");
		// busy wait so output isn't too fast
		for (volatile int i = 0; i < 5000000; i++)
			;
	}
}

void taskB() {
	for (;;) {
		printf("B");
		for (volatile int i = 0; i < 5000000; i++)
			;
	}
}

void taskC() {
	for (;;) {
		printf("C");
		for (volatile int i = 0; i < 5000000; i++)
			;
	}
}

// --- timer handler that drives the scheduler ---

static void timer_handler(Registers *regs) {
	static uint32_t ticks = 0;
	ticks++;
	if (ticks % 200 == 0) // print a dot from the idle context every 2s
		printf(".");
	Scheduler_Tick(regs);
}

void start(BootParams *bootParams) {
	_init();
	HAL_Initialize();

	PMM_Initialize(bootParams, (uint32_t)&__entry_start, (uint32_t)&__end);

	printf("UamiOS v0.1\n");
	printf("Memory: %u KB free\n", PMM_GetFreePages() * 4);

	// Set up scheduler and create three tasks
	Scheduler_Initialize();
	Scheduler_CreateTask("taskA", taskA);
	Scheduler_CreateTask("taskB", taskB);
	Scheduler_CreateTask("taskC", taskC);

	// Timer drives the scheduler
	i686_Timer_Initialize(100);
	i686_IRQ_RegisterHandler(0,
							 timer_handler); // override default timer handler
	i686_IRQ_Unmask(0);

	i686_Keyboard_Initialize();

	printf("Scheduler running. Tasks: A B C\n");

	// Idle loop — the scheduler will preempt this
	for (;;)
		;
}
