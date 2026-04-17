#include "scheduler.h"
#include "../bootloader/stage2/string.h"
#include "memory.h"
#include "pmm.h"
#include <debug.h>
#include <stddef.h>
#include <stdio.h>

#define MODULE "SCHED"

// How many timer ticks between context switches
#define SCHEDULER_TICKS 50

static Task g_Tasks[MAX_TASKS];
static int g_TaskCount = 0;
static int g_CurrentTask = 0;
static int g_TickCount = 0;

// Defined in scheduler_asm.asm — does the actual register swap
extern void Scheduler_Switch(uint32_t *oldEsp, uint32_t newEsp);

void Scheduler_Initialize() {
	memset(g_Tasks, 0, sizeof(g_Tasks));
	g_TaskCount = 0;
	g_CurrentTask = 0;
	g_TickCount = 0;
	log_info(MODULE, "Initialized");
}

void Scheduler_CreateTask(const char *name, void (*entry)()) {
	if (g_TaskCount >= MAX_TASKS) {
		log_err(MODULE, "Max tasks reached");
		return;
	}

	Task *task = &g_Tasks[g_TaskCount];

	// Allocate a stack page
	task->stack = (uint32_t *)PMM_AllocPage();
	if (!task->stack) {
		log_err(MODULE, "Failed to allocate stack for %s", name);
		return;
	}

	// Copy name
	int i = 0;
	while (name[i] && i < 31) {
		task->name[i] = name[i];
		i++;
	}
	task->name[i] = '\0';

	// Stack grows downward
	uint32_t *sp = task->stack + (TASK_STACK_SIZE / sizeof(uint32_t));

	// Return address (entry point)
	*--sp = (uint32_t)entry;

	// Simulate pushad frame
	*--sp = 0; // eax
	*--sp = 0; // ecx
	*--sp = 0; // edx
	*--sp = 0; // ebx
	*--sp = 0; // esp (ignored)
	*--sp = 0; // ebp
	*--sp = 0; // esi
	*--sp = 0; // edi

	task->esp = (uint32_t)sp;
	task->state = TASK_READY;

	log_info(MODULE, "Created task '%s' entry=0x%x stack=0x%x", task->name,
			 (uint32_t)entry, task->esp);

	g_TaskCount++;
}

void Scheduler_Tick(Registers *regs) {
	if (g_TaskCount == 0)
		return;

	g_TickCount++;
	if (g_TickCount < SCHEDULER_TICKS)
		return;
	g_TickCount = 0;

	// Find next ready task (round robin)
	int next = (g_CurrentTask + 1) % g_TaskCount;
	int checked = 0;
	while (g_Tasks[next].state != TASK_READY && checked < g_TaskCount) {
		next = (next + 1) % g_TaskCount;
		checked++;
	}

	if (next == g_CurrentTask)
		return; // only one runnable task, keep going

	int prev = g_CurrentTask;
	g_CurrentTask = next;

	g_Tasks[prev].state = TASK_READY;
	g_Tasks[next].state = TASK_RUNNING;

	// Save old ESP into prev task, load new ESP from next task
	Scheduler_Switch(&g_Tasks[prev].esp, g_Tasks[next].esp);
}
