#pragma once

#include <stdint.h>
#include <stdio.h>

typedef int boon;
#define true 1
#define false 0
#define far

typedef struct {
	FILE *File;
} DISK;

boon DISK_Initialize(DISK *disk, const char *fileName);
boon DISK_ReadSectors(DISK *disk, uint32_t lba, uint8_t sectors,
					  void far *dataOut);
