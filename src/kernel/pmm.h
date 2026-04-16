#pragma once
#include <boot/bootparams.h>
#include <stdbool.h>
#include <stdint.h>

#define PAGE_SIZE 4096

void PMM_Initialize(BootParams *bootParams, uint32_t kernelStart,
					uint32_t kernelEnd);
void *PMM_AllocPage();
void PMM_FreePage(void *page);
uint32_t PMM_GetFreePages();
uint32_t PMM_GetTotalPages();
