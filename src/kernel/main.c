#include "memory.h"
#include "pmm.h"
#include "scheduler.h"
#include "shell.h"
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

static void timer_handler(Registers *regs) { g_NeedSchedule = 1; }

// Shell task — runs the interactive shell
static void shell_task() {
	Shell_Initialize();
	Shell_Run();
}

void start(BootParams *bootParams) {
	_init();
	HAL_Initialize();

	PMM_Initialize(bootParams, (uint32_t)&__entry_start, (uint32_t)&__end);

	printf("UamiOS v0.1\n");
	printf("Memory: %u KB free\n", PMM_GetFreePages() * 4);
	printf("Type 'help' for available commands.\n");

	Scheduler_Initialize();
	Scheduler_RegisterIdle();
	Scheduler_CreateTask("shell", shell_task);

	i686_Timer_Initialize(100);
	i686_IRQ_RegisterHandler(0, timer_handler);
	i686_IRQ_Unmask(0);

	i686_Keyboard_Initialize();

	// Idle loop
	for (;;) {
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}

		__asm__ volatile("hlt");
	}
}
