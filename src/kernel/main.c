#include "memory.h"
#include "stdio.h"
#include <arch/i686/irq.h>
#include <arch/i686/keyboard.h>
#include <arch/i686/timer.h>
#include <boot/bootparams.h>
#include <debug.h>
#include <hal/hal.h>
#include <stdint.h>

extern void _init();

void start(BootParams *bootParams) {
	_init();
	HAL_Initialize();

	log_debug("Main", "Boot device: %x", bootParams->BootDevice);
	log_debug("Main", "Memory regions: %d", bootParams->Memory.RegionCount);
	for (int i = 0; i < bootParams->Memory.RegionCount; i++) {
		log_debug("Main", "MEM: start=0x%llx length=0x%llx type=%x",
				  bootParams->Memory.Regions[i].Begin,
				  bootParams->Memory.Regions[i].Length,
				  bootParams->Memory.Regions[i].Type);
	}

	printf("UamiOS v0.1\n");
	printf("Interrupts: initializing timer and keyboard...\n");

	i686_Timer_Initialize(100); // 100 Hz — dots appear ~10x/second
	i686_Keyboard_Initialize();

	printf("Type something:\n");

	for (;;)
		;
}
