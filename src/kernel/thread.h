// src/kernel/thread.h
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef int ThreadID;

typedef struct {
	ThreadID id;
	char name[32];
	int state;
	uint32_t schedule_count;
} ThreadInfo;

// Create a thread — returns thread ID or -1 on failure
ThreadID Thread_Create(const char *name, void (*entry)());

// Kill a thread by ID
bool Thread_Kill(ThreadID id);

// Yield CPU to next thread
void Thread_Yield();

// Sleep for approximately N timer ticks
void Thread_Sleep(uint32_t ticks);

// Get info about all threads
int Thread_GetCount();
bool Thread_GetInfo(int idx, ThreadInfo *info);
