#pragma once
#include <stdbool.h>
#include <stdint.h>

// ATA PIO driver - reads/writes sectors using port I/O
// Works with QEMU's emulated IDE controller

#define ATA_PRIMARY_BASE 0x1F0
#define ATA_PRIMARY_CTRL 0x3F6

bool ATA_Initialize();
bool ATA_ReadSectors(uint32_t lba, uint8_t count, void *buffer);
bool ATA_WriteSectors(uint32_t lba, uint8_t count, const void *buffer);
