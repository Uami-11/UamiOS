#pragma once
#include <arch/i686/isr.h>
#include <stdint.h>

#define MAX_TASKS 8
#define TASK_STACK_SIZE 4096

extern volatile int g_NeedSchedule;

typedef enum {
	TASK_READY,
	TASK_RUNNING,
	TASK_DEAD,
} TaskState;

typedef struct {
	uint32_t esp;
	uint32_t *stack;
	TaskState state;
	char name[32];
	uint32_t schedule_count; // ← new: how many times this task was scheduled
} Task;

void Scheduler_Initialize();
void Scheduler_CreateTask(const char *name, void (*entry)());
void Scheduler_Tick(Registers *regs);
void Scheduler_RegisterIdle();
void Scheduler_Yield();
int Scheduler_GetTaskCount();
void Scheduler_GetTask(int idx, char *nameOut, int *stateOut,
					   uint32_t *countOut);
void Scheduler_KillTask(int idx);
uint32_t Scheduler_GetTicks();
uint32_t Scheduler_GetScheduleCount(int idx);
