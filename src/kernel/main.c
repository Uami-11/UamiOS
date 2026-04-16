#include "memory.h"
#include "pmm.h"
#include "stdio.h"
#include <arch/i686/irq.h>
#include <arch/i686/keyboard.h>
#include <arch/i686/timer.h>
#include <boot/bootparams.h>
#include <debug.h>
#include <hal/hal.h>
#include <stdint.h>

// These symbols come from the linker script — mark kernel boundaries
extern uint32_t __entry_start;
extern uint32_t __end;

extern void _init();

void start(BootParams *bootParams) {
	_init();
	HAL_Initialize();

	// Physical memory manager
	PMM_Initialize(bootParams, (uint32_t)&__entry_start, (uint32_t)&__end);

	printf("UamiOS v0.1\n");
	printf("Memory: %u KB free\n", PMM_GetFreePages() * 4);

	// Demo: allocate and free a few pages
	void *a = PMM_AllocPage();
	void *b = PMM_AllocPage();
	void *c = PMM_AllocPage();
	printf("Allocated pages: 0x%x  0x%x  0x%x\n", (uint32_t)a, (uint32_t)b,
		   (uint32_t)c);

	PMM_FreePage(b);
	printf("Freed page 0x%x, free now: %u KB\n", (uint32_t)b,
		   PMM_GetFreePages() * 4);

	// Interrupts
	// i686_Timer_Initialize(100);
	i686_Keyboard_Initialize();
	printf("Interrupts active. Type something:\n");

	for (;;)
		;
}
