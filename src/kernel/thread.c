// src/kernel/thread.c
#include "thread.h"
#include "scheduler.h"
#include <debug.h>
#include <stdio.h>

#define MODULE "THREAD"

ThreadID Thread_Create(const char *name, void (*entry)()) {
	int before = Scheduler_GetTaskCount();
	Scheduler_CreateTask(name, entry);
	int after = Scheduler_GetTaskCount();
	if (after == before)
		return -1;
	log_info(MODULE, "Created thread '%s' id=%d", name, after - 1);
	return after - 1;
}

bool Thread_Kill(ThreadID id) {
	if (id <= 0 || id >= Scheduler_GetTaskCount())
		return false;
	Scheduler_KillTask(id);
	return true;
}

void Thread_Yield() {
	g_NeedSchedule = 1;
	Scheduler_Yield();
}

void Thread_Sleep(uint32_t ticks) {
	uint32_t start = Scheduler_GetTicks();
	while (Scheduler_GetTicks() - start < ticks) {
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}
}

int Thread_GetCount() { return Scheduler_GetTaskCount(); }

bool Thread_GetInfo(int idx, ThreadInfo *info) {
	if (idx < 0 || idx >= Scheduler_GetTaskCount())
		return false;
	char name[33];
	int state;
	uint32_t sc;
	Scheduler_GetTask(idx, name, &state, &sc);
	info->id = idx;
	info->state = state;
	info->schedule_count = sc;
	int i = 0;
	while (name[i] && i < 31) {
		info->name[i] = name[i];
		i++;
	}
	info->name[i] = '\0';
	return true;
}
