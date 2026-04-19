#pragma once
#include <arch/i686/isr.h>
#include <stdint.h>

#define MAX_TASKS 8
#define TASK_STACK_SIZE 4096 // one page per task
extern volatile int g_NeedSchedule;

typedef enum {
	TASK_READY,
	TASK_RUNNING,
	TASK_DEAD,
} TaskState;

typedef struct {
	uint32_t esp;	 // saved stack pointer — must be first
	uint32_t *stack; // base of allocated stack
	TaskState state;
	char name[32];
} Task;

void Scheduler_Initialize();
void Scheduler_CreateTask(const char *name, void (*entry)());
void Scheduler_Tick(Registers *regs); // called by timer IRQ
void Scheduler_RegisterIdle();
void Scheduler_Yield();
