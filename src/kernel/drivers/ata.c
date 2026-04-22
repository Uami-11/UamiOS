#include "ata.h"
#include <arch/i686/io.h>
#include <debug.h>
#include <stdio.h>

#define MODULE "ATA"

// ATA registers (offset from base)
#define ATA_REG_DATA 0x00
#define ATA_REG_ERROR 0x01
#define ATA_REG_SECCOUNT 0x02
#define ATA_REG_LBA0 0x03
#define ATA_REG_LBA1 0x04
#define ATA_REG_LBA2 0x05
#define ATA_REG_DRIVE 0x06
#define ATA_REG_STATUS 0x07
#define ATA_REG_COMMAND 0x07

// Status bits
#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08 // data ready
#define ATA_STATUS_SRV 0x10
#define ATA_STATUS_DF 0x20 // drive fault
#define ATA_STATUS_RDY 0x40
#define ATA_STATUS_BSY 0x80 // busy

// Commands
#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_FLUSH 0xE7
#define ATA_CMD_IDENTIFY 0xEC

static uint16_t g_Base = ATA_PRIMARY_BASE;

static void ata_wait_bsy() {
	while (i686_inb(g_Base + ATA_REG_STATUS) & ATA_STATUS_BSY)
		;
}

static bool ata_wait_drq() {
	uint8_t status;
	// wait up to ~30000 iterations
	for (int i = 0; i < 30000; i++) {
		status = i686_inb(g_Base + ATA_REG_STATUS);
		if (status & ATA_STATUS_ERR)
			return false;
		if (status & ATA_STATUS_DF)
			return false;
		if (status & ATA_STATUS_DRQ)
			return true;
	}
	return false;
}

static void ata_select_lba(uint32_t lba, uint8_t count) {
	// LBA28 mode, master drive (0xE0)
	i686_outb(g_Base + ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
	i686_outb(g_Base + ATA_REG_SECCOUNT, count);
	i686_outb(g_Base + ATA_REG_LBA0, (uint8_t)(lba));
	i686_outb(g_Base + ATA_REG_LBA1, (uint8_t)(lba >> 8));
	i686_outb(g_Base + ATA_REG_LBA2, (uint8_t)(lba >> 16));
}

bool ATA_Initialize() {
	// Send IDENTIFY command to check if drive is present
	i686_outb(g_Base + ATA_REG_DRIVE, 0xA0); // select master
	i686_outb(g_Base + ATA_REG_SECCOUNT, 0);
	i686_outb(g_Base + ATA_REG_LBA0, 0);
	i686_outb(g_Base + ATA_REG_LBA1, 0);
	i686_outb(g_Base + ATA_REG_LBA2, 0);
	i686_outb(g_Base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

	uint8_t status = i686_inb(g_Base + ATA_REG_STATUS);
	if (status == 0) {
		log_err(MODULE, "No drive found");
		return false;
	}

	ata_wait_bsy();

	// Check it's actually ATA (not ATAPI)
	uint8_t cl = i686_inb(g_Base + ATA_REG_LBA1);
	uint8_t ch = i686_inb(g_Base + ATA_REG_LBA2);
	if (cl != 0 || ch != 0) {
		log_err(MODULE, "Not an ATA device");
		return false;
	}

	if (!ata_wait_drq()) {
		log_err(MODULE, "IDENTIFY failed");
		return false;
	}

	// Read and discard the 256-word identify data
	for (int i = 0; i < 256; i++)
		i686_inw(g_Base + ATA_REG_DATA);

	log_info(MODULE, "ATA drive ready");
	return true;
}

bool ATA_ReadSectors(uint32_t lba, uint8_t count, void *buffer) {
	uint16_t *buf = (uint16_t *)buffer;

	ata_wait_bsy();
	ata_select_lba(lba, count);
	i686_outb(g_Base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

	for (int s = 0; s < count; s++) {
		ata_wait_bsy();
		if (!ata_wait_drq()) {
			log_err(MODULE, "Read failed at LBA %u", lba + s);
			return false;
		}
		// Read 256 words (512 bytes) per sector
		for (int i = 0; i < 256; i++)
			*buf++ = i686_inw(g_Base + ATA_REG_DATA);
	}
	return true;
}

bool ATA_WriteSectors(uint32_t lba, uint8_t count, const void *buffer) {
	const uint16_t *buf = (const uint16_t *)buffer;

	ata_wait_bsy();
	ata_select_lba(lba, count);
	i686_outb(g_Base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

	for (int s = 0; s < count; s++) {
		ata_wait_bsy();
		if (!ata_wait_drq()) {
			log_err(MODULE, "Write failed at LBA %u", lba + s);
			return false;
		}
		for (int i = 0; i < 256; i++)
			i686_outw(g_Base + ATA_REG_DATA, *buf++);
	}

	// Flush write cache
	i686_outb(g_Base + ATA_REG_COMMAND, ATA_CMD_FLUSH);
	ata_wait_bsy();
	return true;
}
