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
	int count = 0;
	for (;;) {
		printf("[A:%d] ", count++);
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}
}

void taskB() {
	int count = 0;
	for (;;) {
		printf("[B:%d] ", count++);
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}
}

void taskC() {
	int count = 0;
	for (;;) {
		printf("[C:%d] ", count++);
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}
}

static void timer_handler(Registers *regs) {
	static uint32_t ticks = 0;
	ticks++;
	g_NeedSchedule = 1; // just signal, don't switch here
}

void start(BootParams *bootParams) {
	_init();
	HAL_Initialize();

	PMM_Initialize(bootParams, (uint32_t)&__entry_start, (uint32_t)&__end);

	printf("UamiOS v0.1\n");
	printf("Memory: %u KB free\n", PMM_GetFreePages() * 4);

	Scheduler_Initialize();
	Scheduler_RegisterIdle();
	Scheduler_CreateTask("taskA", taskA);
	Scheduler_CreateTask("taskB", taskB);
	Scheduler_CreateTask("taskC", taskC);

	i686_Timer_Initialize(100);
	i686_IRQ_RegisterHandler(0, timer_handler);
	i686_IRQ_Unmask(0);

	i686_Keyboard_Initialize();

	printf("Scheduler running. Tasks: A B C\n");

	// Main loop — drives the scheduler from normal context
	for (;;) {
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}
}
