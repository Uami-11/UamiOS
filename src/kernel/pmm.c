#include "pmm.h"
#include "memory.h"
#include <debug.h>
#include <stdio.h>

#define MODULE "PMM"
#define MAX_PAGES (32 * 1024)		// supports up to 128 MB
#define BITMAP_SIZE (MAX_PAGES / 8) // 1 bit per page

// Bitmap: bit=0 means free, bit=1 means used
static uint8_t g_Bitmap[BITMAP_SIZE];
static uint32_t g_TotalPages = 0;
static uint32_t g_FreePages = 0;

// Mark a single page as used
static void bitmap_set(uint32_t page) {
	g_Bitmap[page / 8] |= (1 << (page % 8));
}

// Mark a single page as free
static void bitmap_clear(uint32_t page) {
	g_Bitmap[page / 8] &= ~(1 << (page % 8));
}

// Check if a page is used
static bool bitmap_test(uint32_t page) {
	return g_Bitmap[page / 8] & (1 << (page % 8));
}

void PMM_Initialize(BootParams *bootParams, uint32_t kernelStart,
					uint32_t kernelEnd) {
	// Start with everything marked as used
	memset(g_Bitmap, 0xFF, sizeof(g_Bitmap));

	// Walk E820 map and free usable regions (type == 1)
	for (int i = 0; i < bootParams->Memory.RegionCount; i++) {
		MemoryRegion *region = &bootParams->Memory.Regions[i];

		if (region->Type != 1)
			continue;

		// Only handle memory we can address (below 4GB, within MAX_PAGES)
		uint64_t start = region->Begin;
		uint64_t end = start + region->Length;

		// Align start up and end down to page boundaries
		uint32_t pageStart = (uint32_t)((start + PAGE_SIZE - 1) / PAGE_SIZE);
		uint32_t pageEnd = (uint32_t)(end / PAGE_SIZE);

		if (pageEnd > MAX_PAGES)
			pageEnd = MAX_PAGES;

		for (uint32_t p = pageStart; p < pageEnd; p++) {
			if (bitmap_test(p)) {
				bitmap_clear(p);
				g_FreePages++;
				g_TotalPages++;
			}
		}
	}

	// Re-mark the kernel pages as used
	uint32_t kernelPageStart = kernelStart / PAGE_SIZE;
	uint32_t kernelPageEnd = (kernelEnd + PAGE_SIZE - 1) / PAGE_SIZE;

	for (uint32_t p = kernelPageStart; p < kernelPageEnd; p++) {
		if (!bitmap_test(p)) {
			bitmap_set(p);
			g_FreePages--;
		}
	}

	// Also protect page 0 (null pointer trap)
	if (!bitmap_test(0)) {
		bitmap_set(0);
		g_FreePages--;
	}

	log_info(MODULE, "Initialized: %u KB free out of %u KB total",
			 g_FreePages * (PAGE_SIZE / 1024),
			 g_TotalPages * (PAGE_SIZE / 1024));
}

void *PMM_AllocPage() {
	for (uint32_t i = 0; i < MAX_PAGES; i++) {
		if (!bitmap_test(i)) {
			bitmap_set(i);
			g_FreePages--;
			return (void *)(i * PAGE_SIZE);
		}
	}

	log_err(MODULE, "Out of memory!");
	return NULL;
}

void PMM_FreePage(void *page) {
	uint32_t index = (uint32_t)page / PAGE_SIZE;
	if (bitmap_test(index)) {
		bitmap_clear(index);
		g_FreePages++;
	}
}

uint32_t PMM_GetFreePages() { return g_FreePages; }
uint32_t PMM_GetTotalPages() { return g_TotalPages; }
