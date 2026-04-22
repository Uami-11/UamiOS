#include "fat.h"
#include <debug.h>
#include <drivers/ata.h>
#include <memory.h>
#include <stddef.h>
#include <stdio.h>

#define MODULE "FAT"
#define SECTOR_SIZE 512
#define MAX_FILE_HANDLES 10
#define ROOT_DIR_HANDLE -1
#define FAT_CACHE_SIZE 5
#define MAX_PATH 256

// ── boot sector structures ─────────────────────────────────────────────────
static int strlen_simple(const char *s);

typedef struct {
	uint8_t DriveNumber;
	uint8_t _Reserved;
	uint8_t Signature;
	uint32_t VolumeId;
	uint8_t VolumeLabel[11];
	uint8_t SystemId[8];
} __attribute__((packed)) FAT_EBR;

typedef struct {
	uint32_t SectorsPerFat;
	uint16_t Flags;
	uint16_t FatVersion;
	uint32_t RootDirCluster;
	uint16_t FSInfoSector;
	uint16_t BackupBootSector;
	uint8_t _Reserved[12];
	FAT_EBR EBR;
} __attribute__((packed)) FAT32_EBR;

typedef struct {
	uint8_t BootJump[3];
	uint8_t OemId[8];
	uint16_t BytesPerSector;
	uint8_t SectorsPerCluster;
	uint16_t ReservedSectors;
	uint8_t FatCount;
	uint16_t DirEntryCount;
	uint16_t TotalSectors16;
	uint8_t MediaDescriptor;
	uint16_t SectorsPerFat16;
	uint16_t SectorsPerTrack;
	uint16_t Heads;
	uint32_t HiddenSectors;
	uint32_t TotalSectors32;
	union {
		FAT_EBR EBR1216;
		FAT32_EBR EBR32;
	};
} __attribute__((packed)) FAT_BootSector;

// ── internal file data ─────────────────────────────────────────────────────

typedef struct {
	uint8_t Buffer[SECTOR_SIZE];
	FAT_File Public;
	bool Opened;
	uint32_t FirstCluster;
	uint32_t CurrentCluster;
	uint32_t CurrentSectorInCluster;
} FAT_FileData;

typedef struct {
	union {
		FAT_BootSector BootSector;
		uint8_t BootSectorBytes[SECTOR_SIZE];
	} BS;
	FAT_FileData RootDirectory;
	FAT_FileData OpenedFiles[MAX_FILE_HANDLES];
	uint8_t FatCache[FAT_CACHE_SIZE * SECTOR_SIZE];
	uint32_t FatCachePosition;
} FAT_Data;

// Placed at a fixed kernel memory address — 64KB after the kernel heap area
// Adjust if this conflicts with your memory map
static FAT_Data g_FatData;
static FAT_Data *g_Data = &g_FatData;

static uint32_t g_PartitionOffset = 2048; // your partition starts here
static uint32_t g_DataSectionLba;
static uint8_t g_FatType;
static uint32_t g_TotalSectors;
static uint32_t g_SectorsPerFat;

// ── low-level I/O ──────────────────────────────────────────────────────────

static bool disk_read(uint32_t lba, uint8_t count, void *buf) {
	return ATA_ReadSectors(g_PartitionOffset + lba, count, buf);
}

static bool disk_write(uint32_t lba, uint8_t count, const void *buf) {
	return ATA_WriteSectors(g_PartitionOffset + lba, count, buf);
}

// ── string helpers ─────────────────────────────────────────────────────────

static char fat_toupper(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static void fat_get_short_name(const char *name, char out[12]) {
	memset(out, ' ', 11);
	out[11] = '\0';

	const char *dot = 0;
	for (int i = 0; name[i]; i++)
		if (name[i] == '.')
			dot = name + i;

	int base_len = dot ? (int)(dot - name) : (int)strlen_simple(name);
	if (base_len > 8)
		base_len = 8;

	for (int i = 0; i < base_len; i++)
		out[i] = fat_toupper(name[i]);

	if (dot) {
		int ext_len = 0;
		for (int i = 1; dot[i] && ext_len < 3; i++, ext_len++)
			out[8 + ext_len] = fat_toupper(dot[i]);
	}
}

static int strlen_simple(const char *s) {
	int n = 0;
	while (s[n])
		n++;
	return n;
}

static int strcmp_simple(const char *a, const char *b) {
	while (*a && *b && *a == *b) {
		a++;
		b++;
	}
	return *a - *b;
}

static void memcpy_simple(void *dst, const void *src, uint32_t n) {
	uint8_t *d = dst;
	const uint8_t *s = src;
	for (uint32_t i = 0; i < n; i++)
		d[i] = s[i];
}

static void memset_simple(void *dst, uint8_t v, uint32_t n) {
	uint8_t *d = dst;
	for (uint32_t i = 0; i < n; i++)
		d[i] = v;
}

static int memcmp_simple(const void *a, const void *b, uint32_t n) {
	const uint8_t *p = a, *q = b;
	for (uint32_t i = 0; i < n; i++)
		if (p[i] != q[i])
			return p[i] - q[i];
	return 0;
}

// ── FAT access ─────────────────────────────────────────────────────────────

static bool fat_read_fat(uint32_t lbaIndex) {
	return disk_read(g_Data->BS.BootSector.ReservedSectors + lbaIndex,
					 FAT_CACHE_SIZE, g_Data->FatCache);
}

static bool fat_write_fat(uint32_t lbaIndex) {
	return disk_write(g_Data->BS.BootSector.ReservedSectors + lbaIndex,
					  FAT_CACHE_SIZE, g_Data->FatCache);
}

static uint32_t fat_cluster_to_lba(uint32_t cluster) {
	return g_DataSectionLba +
		   (cluster - 2) * g_Data->BS.BootSector.SectorsPerCluster;
}

static uint32_t fat_next_cluster(uint32_t current) {
	uint32_t fatIndex = current * 4; // FAT32 only
	uint32_t sector = fatIndex / SECTOR_SIZE;

	if (sector < g_Data->FatCachePosition ||
		sector >= g_Data->FatCachePosition + FAT_CACHE_SIZE) {
		fat_read_fat(sector);
		g_Data->FatCachePosition = sector;
	}

	fatIndex -= g_Data->FatCachePosition * SECTOR_SIZE;
	return *(uint32_t *)(g_Data->FatCache + fatIndex) & 0x0FFFFFFF;
}

static void fat_set_cluster(uint32_t cluster, uint32_t value) {
	uint32_t fatIndex = cluster * 4;
	uint32_t sector = fatIndex / SECTOR_SIZE;

	if (sector < g_Data->FatCachePosition ||
		sector >= g_Data->FatCachePosition + FAT_CACHE_SIZE) {
		fat_read_fat(sector);
		g_Data->FatCachePosition = sector;
	}

	uint32_t offset = fatIndex - g_Data->FatCachePosition * SECTOR_SIZE;
	uint32_t *entry = (uint32_t *)(g_Data->FatCache + offset);
	*entry = (*entry & 0xF0000000) | (value & 0x0FFFFFFF);

	fat_write_fat(g_Data->FatCachePosition);
}

// Find a free cluster in the FAT
static uint32_t fat_alloc_cluster() {
	uint32_t total_clusters =
		g_TotalSectors / g_Data->BS.BootSector.SectorsPerCluster;
	for (uint32_t c = 2; c < total_clusters; c++) {
		if (fat_next_cluster(c) == 0) {
			fat_set_cluster(c, 0x0FFFFFFF); // mark end of chain
			return c;
		}
	}
	return 0; // no free cluster
}

// ── initialize ─────────────────────────────────────────────────────────────

bool FAT_Initialize() {
	if (!disk_read(0, 1, g_Data->BS.BootSectorBytes)) {
		log_err(MODULE, "Failed to read boot sector");
		return false;
	}

	g_Data->FatCachePosition = 0xFFFFFFFF;

	g_TotalSectors = g_Data->BS.BootSector.TotalSectors16;
	if (g_TotalSectors == 0)
		g_TotalSectors = g_Data->BS.BootSector.TotalSectors32;

	g_SectorsPerFat = g_Data->BS.BootSector.SectorsPerFat16;
	bool isFat32 = (g_SectorsPerFat == 0);
	if (isFat32)
		g_SectorsPerFat = g_Data->BS.BootSector.EBR32.SectorsPerFat;

	if (!isFat32) {
		log_err(MODULE, "Only FAT32 supported in kernel driver");
		return false;
	}

	g_DataSectionLba = g_Data->BS.BootSector.ReservedSectors +
					   g_SectorsPerFat * g_Data->BS.BootSector.FatCount;

	uint32_t rootCluster = g_Data->BS.BootSector.EBR32.RootDirCluster;
	uint32_t rootLba = fat_cluster_to_lba(rootCluster);

	g_Data->RootDirectory.Public.Handle = ROOT_DIR_HANDLE;
	g_Data->RootDirectory.Public.IsDirectory = true;
	g_Data->RootDirectory.Public.Position = 0;
	g_Data->RootDirectory.Public.Size = 0;
	g_Data->RootDirectory.Opened = true;
	g_Data->RootDirectory.FirstCluster = rootCluster;
	g_Data->RootDirectory.CurrentCluster = rootCluster;
	g_Data->RootDirectory.CurrentSectorInCluster = 0;

	if (!disk_read(rootLba, 1, g_Data->RootDirectory.Buffer)) {
		log_err(MODULE, "Failed to read root directory");
		return false;
	}

	for (int i = 0; i < MAX_FILE_HANDLES; i++)
		g_Data->OpenedFiles[i].Opened = false;

	log_info(MODULE, "FAT32 initialized. Root cluster=%u DataLBA=%u",
			 rootCluster, g_DataSectionLba);
	return true;
}

// ── open / read / close ────────────────────────────────────────────────────

static FAT_File *fat_open_entry(FAT_DirectoryEntry *entry) {
	int handle = -1;
	for (int i = 0; i < MAX_FILE_HANDLES; i++)
		if (!g_Data->OpenedFiles[i].Opened) {
			handle = i;
			break;
		}

	if (handle < 0) {
		log_err(MODULE, "Out of file handles");
		return NULL;
	}

	FAT_FileData *fd = &g_Data->OpenedFiles[handle];
	fd->Public.Handle = handle;
	fd->Public.IsDirectory = (entry->Attributes & FAT_ATTRIBUTE_DIRECTORY) != 0;
	fd->Public.Position = 0;
	fd->Public.Size = entry->Size;
	fd->FirstCluster =
		entry->FirstClusterLow | ((uint32_t)entry->FirstClusterHigh << 16);
	fd->CurrentCluster = fd->FirstCluster;
	fd->CurrentSectorInCluster = 0;
	fd->Opened = true;

	if (!disk_read(fat_cluster_to_lba(fd->CurrentCluster), 1, fd->Buffer)) {
		fd->Opened = false;
		return NULL;
	}
	return &fd->Public;
}

static bool fat_find_in_dir(FAT_File *dir, const char *name,
							FAT_DirectoryEntry *out) {
	char shortName[12];
	fat_get_short_name(name, shortName);

	FAT_DirectoryEntry entry;
	// reset dir position
	FAT_FileData *fd = (dir->Handle == ROOT_DIR_HANDLE)
						   ? &g_Data->RootDirectory
						   : &g_Data->OpenedFiles[dir->Handle];
	fd->Public.Position = 0;
	fd->CurrentCluster = fd->FirstCluster;
	fd->CurrentSectorInCluster = 0;
	disk_read(fat_cluster_to_lba(fd->CurrentCluster), 1, fd->Buffer);

	while (FAT_ReadEntry(dir, &entry)) {
		if (entry.Name[0] == 0x00)
			break;
		if ((uint8_t)entry.Name[0] == 0xE5)
			continue;
		if (entry.Attributes == FAT_ATTRIBUTE_LFN)
			continue;
		if (memcmp_simple(shortName, entry.Name, 11) == 0) {
			*out = entry;
			return true;
		}
	}
	return false;
}

FAT_File *FAT_Open(const char *path) {
	if (*path == '/')
		path++;

	FAT_File *current = &g_Data->RootDirectory.Public;
	// reset root
	g_Data->RootDirectory.Public.Position = 0;
	g_Data->RootDirectory.CurrentCluster = g_Data->RootDirectory.FirstCluster;
	g_Data->RootDirectory.CurrentSectorInCluster = 0;
	disk_read(fat_cluster_to_lba(g_Data->RootDirectory.CurrentCluster), 1,
			  g_Data->RootDirectory.Buffer);

	char name[MAX_PATH];
	while (*path) {
		// extract component
		int len = 0;
		while (path[len] && path[len] != '/')
			len++;
		memcpy_simple(name, path, len);
		name[len] = '\0';
		path += len;
		if (*path == '/')
			path++;

		FAT_DirectoryEntry entry;
		if (!fat_find_in_dir(current, name, &entry)) {
			if (current->Handle != ROOT_DIR_HANDLE)
				FAT_Close(current);
			return NULL;
		}

		if (current->Handle != ROOT_DIR_HANDLE)
			FAT_Close(current);

		current = fat_open_entry(&entry);
		if (!current)
			return NULL;
	}
	return current;
}

uint32_t FAT_Read(FAT_File *file, uint32_t byteCount, void *dataOut) {
	FAT_FileData *fd = (file->Handle == ROOT_DIR_HANDLE)
						   ? &g_Data->RootDirectory
						   : &g_Data->OpenedFiles[file->Handle];

	uint8_t *out = (uint8_t *)dataOut;

	if (!fd->Public.IsDirectory || fd->Public.Size != 0)
		if (byteCount > fd->Public.Size - fd->Public.Position)
			byteCount = fd->Public.Size - fd->Public.Position;

	while (byteCount > 0) {
		uint32_t posInSector = fd->Public.Position % SECTOR_SIZE;
		uint32_t left = SECTOR_SIZE - posInSector;
		uint32_t take = byteCount < left ? byteCount : left;

		memcpy_simple(out, fd->Buffer + posInSector, take);
		out += take;
		fd->Public.Position += take;
		byteCount -= take;

		if (left == take) {
			if (file->Handle == ROOT_DIR_HANDLE) {
				// root dir: advance cluster manually
				if (++fd->CurrentSectorInCluster >=
					g_Data->BS.BootSector.SectorsPerCluster) {
					fd->CurrentSectorInCluster = 0;
					fd->CurrentCluster = fat_next_cluster(fd->CurrentCluster);
					if (fd->CurrentCluster >= 0x0FFFFFF8)
						break;
				}
				disk_read(fat_cluster_to_lba(fd->CurrentCluster) +
							  fd->CurrentSectorInCluster,
						  1, fd->Buffer);
			} else {
				if (++fd->CurrentSectorInCluster >=
					g_Data->BS.BootSector.SectorsPerCluster) {
					fd->CurrentSectorInCluster = 0;
					fd->CurrentCluster = fat_next_cluster(fd->CurrentCluster);
					if (fd->CurrentCluster >= 0x0FFFFFF8) {
						fd->Public.Size = fd->Public.Position;
						break;
					}
				}
				disk_read(fat_cluster_to_lba(fd->CurrentCluster) +
							  fd->CurrentSectorInCluster,
						  1, fd->Buffer);
			}
		}
	}
	return out - (uint8_t *)dataOut;
}

bool FAT_ReadEntry(FAT_File *file, FAT_DirectoryEntry *entry) {
	return FAT_Read(file, sizeof(FAT_DirectoryEntry), entry) ==
		   sizeof(FAT_DirectoryEntry);
}

void FAT_Close(FAT_File *file) {
	if (!file)
		return;
	if (file->Handle == ROOT_DIR_HANDLE) {
		file->Position = 0;
		g_Data->RootDirectory.CurrentCluster =
			g_Data->RootDirectory.FirstCluster;
		g_Data->RootDirectory.CurrentSectorInCluster = 0;
	} else {
		g_Data->OpenedFiles[file->Handle].Opened = false;
	}
}

// ── directory listing ──────────────────────────────────────────────────────

bool FAT_ListDir(const char *path, FAT_ListCallback cb) {
	FAT_File *dir;

	if (path == NULL || path[0] == '\0' ||
		(path[0] == '/' && path[1] == '\0')) {
		dir = &g_Data->RootDirectory.Public;
		g_Data->RootDirectory.Public.Position = 0;
		g_Data->RootDirectory.CurrentCluster =
			g_Data->RootDirectory.FirstCluster;
		g_Data->RootDirectory.CurrentSectorInCluster = 0;

		disk_read(fat_cluster_to_lba(g_Data->RootDirectory.CurrentCluster), 1,
				  g_Data->RootDirectory.Buffer);
	} else {
		dir = FAT_Open(path);
		if (!dir || !dir->IsDirectory) {
			if (dir)
				FAT_Close(dir);
			return false;
		}
	}

	FAT_DirectoryEntry entry;

	while (FAT_ReadEntry(dir, &entry)) {

		// End of directory
		if (entry.Name[0] == 0x00)
			break;

		// Deleted entry
		if ((uint8_t)entry.Name[0] == 0xE5)
			continue;

		// Long File Name entries
		if (entry.Attributes == FAT_ATTRIBUTE_LFN)
			continue;

		// Skip volume label + system entries
		if (entry.Attributes & FAT_ATTRIBUTE_VOLUME_ID)
			continue;
		if (entry.Attributes & FAT_ATTRIBUTE_SYSTEM)
			continue;

		// Validate first character (must be printable ASCII)
		uint8_t first = (uint8_t)entry.Name[0];
		if (first < 0x20 || first == 0x7F)
			continue;
		if (first == ' ')
			continue;

		// ── Convert 8.3 name ─────────────────────────────
		char name[13];
		int pos = 0;

		// Base name (trim trailing spaces)
		int base_end = 8;
		while (base_end > 0 && entry.Name[base_end - 1] == ' ')
			base_end--;

		for (int i = 0; i < base_end; i++)
			name[pos++] = entry.Name[i];

		// Extension
		int ext_end = 11;
		while (ext_end > 8 && entry.Name[ext_end - 1] == ' ')
			ext_end--;

		if (ext_end > 8) {
			name[pos++] = '.';
			for (int i = 8; i < ext_end; i++)
				name[pos++] = entry.Name[i];
		}

		name[pos] = '\0';

		bool isDir = (entry.Attributes & FAT_ATTRIBUTE_DIRECTORY) != 0;
		cb(name, isDir, entry.Size);
	}

	if (dir->Handle != ROOT_DIR_HANDLE)
		FAT_Close(dir);

	return true;
}

// ── write operations ───────────────────────────────────────────────────────

// Find a free directory entry slot in a directory cluster chain
// Returns the LBA and offset within the sector where the slot is
static bool fat_find_free_dir_slot(uint32_t dirCluster, uint32_t *slotLba,
								   uint32_t *slotOffset) {
	uint8_t sector[SECTOR_SIZE];
	uint32_t cluster = dirCluster;
	uint8_t spc = g_Data->BS.BootSector.SectorsPerCluster;

	while (cluster < 0x0FFFFFF8) {
		uint32_t lba = fat_cluster_to_lba(cluster);
		for (uint8_t s = 0; s < spc; s++) {
			if (!disk_read(lba + s, 1, sector))
				return false;
			for (int e = 0; e < SECTOR_SIZE / 32; e++) {
				uint8_t first = sector[e * 32];
				if (first == 0x00 || first == 0xE5) {
					*slotLba = lba + s;
					*slotOffset = e * 32;
					return true;
				}
			}
		}
		cluster = fat_next_cluster(cluster);
	}
	return false;
}

static uint32_t fat_get_dir_cluster(const char *path) {
	if (path == NULL || path[0] == '\0' || (path[0] == '/' && path[1] == '\0'))
		return g_Data->BS.BootSector.EBR32.RootDirCluster;

	FAT_File *dir = FAT_Open(path);
	if (!dir)
		return 0;
	FAT_FileData *fd = &g_Data->OpenedFiles[dir->Handle];
	uint32_t cluster = fd->FirstCluster;
	FAT_Close(dir);
	return cluster;
}

bool FAT_CreateFile(const char *path) {
	// Split into directory and filename
	const char *slash = 0;
	for (int i = 0; path[i]; i++)
		if (path[i] == '/')
			slash = path + i;

	char dirPath[MAX_PATH] = "";
	char fileName[MAX_PATH] = "";

	if (slash) {
		int dlen = (int)(slash - path);
		memcpy_simple(dirPath, path, dlen);
		dirPath[dlen] = '\0';
		int flen = strlen_simple(slash + 1);
		memcpy_simple(fileName, slash + 1, flen);
		fileName[flen] = '\0';
	} else {
		int flen = strlen_simple(path);
		memcpy_simple(fileName, path, flen);
		fileName[flen] = '\0';
	}

	uint32_t dirCluster = fat_get_dir_cluster(dirPath[0] ? dirPath : NULL);
	if (!dirCluster)
		return false;

	uint32_t slotLba, slotOffset;
	if (!fat_find_free_dir_slot(dirCluster, &slotLba, &slotOffset))
		return false;

	uint8_t sector[SECTOR_SIZE];
	disk_read(slotLba, 1, sector);

	FAT_DirectoryEntry *entry = (FAT_DirectoryEntry *)(sector + slotOffset);
	memset_simple(entry, 0, sizeof(FAT_DirectoryEntry));

	char shortName[12];
	fat_get_short_name(fileName, shortName);
	memcpy_simple(entry->Name, shortName, 11);

	entry->Attributes = FAT_ATTRIBUTE_ARCHIVE;
	entry->FirstClusterHigh = 0;
	entry->FirstClusterLow = 0;
	entry->Size = 0;

	return disk_write(slotLba, 1, sector);
}

bool FAT_CreateDir(const char *path) {
	const char *slash = 0;
	for (int i = 0; path[i]; i++)
		if (path[i] == '/')
			slash = path + i;

	char dirPath[MAX_PATH] = "";
	char dirName[MAX_PATH] = "";

	if (slash) {
		int dlen = (int)(slash - path);
		memcpy_simple(dirPath, path, dlen);
		dirPath[dlen] = '\0';
		memcpy_simple(dirName, slash + 1, strlen_simple(slash + 1));
	} else {
		memcpy_simple(dirName, path, strlen_simple(path));
	}

	uint32_t parentCluster = fat_get_dir_cluster(dirPath[0] ? dirPath : NULL);
	if (!parentCluster)
		return false;

	// Allocate a cluster for the new directory
	uint32_t newCluster = fat_alloc_cluster();
	if (!newCluster)
		return false;

	// Zero out the new cluster
	uint8_t zeroes[SECTOR_SIZE];
	memset_simple(zeroes, 0, SECTOR_SIZE);
	uint32_t lba = fat_cluster_to_lba(newCluster);
	for (int s = 0; s < g_Data->BS.BootSector.SectorsPerCluster; s++)
		disk_write(lba + s, 1, zeroes);

	// Write . and .. entries
	uint8_t sector[SECTOR_SIZE];
	memset_simple(sector, 0, SECTOR_SIZE);

	FAT_DirectoryEntry *dot = (FAT_DirectoryEntry *)sector;
	memset_simple(dot->Name, ' ', 11);
	dot->Name[0] = '.';
	dot->Attributes = FAT_ATTRIBUTE_DIRECTORY;
	dot->FirstClusterHigh = (uint16_t)(newCluster >> 16);
	dot->FirstClusterLow = (uint16_t)(newCluster);

	FAT_DirectoryEntry *dotdot = dot + 1;
	memset_simple(dotdot->Name, ' ', 11);
	dotdot->Name[0] = '.';
	dotdot->Name[1] = '.';
	dotdot->Attributes = FAT_ATTRIBUTE_DIRECTORY;
	dotdot->FirstClusterHigh = (uint16_t)(parentCluster >> 16);
	dotdot->FirstClusterLow = (uint16_t)(parentCluster);

	disk_write(lba, 1, sector);

	// Add entry in parent directory
	uint32_t slotLba, slotOffset;
	if (!fat_find_free_dir_slot(parentCluster, &slotLba, &slotOffset))
		return false;

	disk_read(slotLba, 1, sector);
	FAT_DirectoryEntry *entry = (FAT_DirectoryEntry *)(sector + slotOffset);
	memset_simple(entry, 0, sizeof(FAT_DirectoryEntry));

	char shortName[12];
	fat_get_short_name(dirName, shortName);
	memcpy_simple(entry->Name, shortName, 11);

	entry->Attributes = FAT_ATTRIBUTE_DIRECTORY;
	entry->FirstClusterHigh = (uint16_t)(newCluster >> 16);
	entry->FirstClusterLow = (uint16_t)(newCluster);
	entry->Size = 0;

	return disk_write(slotLba, 1, sector);
}

bool FAT_DeleteEntry(const char *path) {
	const char *slash = 0;
	for (int i = 0; path[i]; i++)
		if (path[i] == '/')
			slash = path + i;

	char dirPath[MAX_PATH] = "";
	char fileName[MAX_PATH] = "";

	if (slash) {
		int dlen = (int)(slash - path);
		memcpy_simple(dirPath, path, dlen);
		dirPath[dlen] = '\0';
		memcpy_simple(fileName, slash + 1, strlen_simple(slash + 1));
	} else {
		memcpy_simple(fileName, path, strlen_simple(path));
	}

	uint32_t dirCluster = fat_get_dir_cluster(dirPath[0] ? dirPath : NULL);
	if (!dirCluster)
		return false;

	char shortName[12];
	fat_get_short_name(fileName, shortName);

	uint8_t sector[SECTOR_SIZE];
	uint32_t cluster = dirCluster;
	uint8_t spc = g_Data->BS.BootSector.SectorsPerCluster;

	while (cluster < 0x0FFFFFF8) {
		uint32_t lba = fat_cluster_to_lba(cluster);
		for (uint8_t s = 0; s < spc; s++) {
			disk_read(lba + s, 1, sector);
			for (int e = 0; e < SECTOR_SIZE / 32; e++) {
				FAT_DirectoryEntry *entry =
					(FAT_DirectoryEntry *)(sector + e * 32);
				if (entry->Name[0] == 0x00)
					return false;
				if ((uint8_t)entry->Name[0] == 0xE5)
					continue;
				if (entry->Attributes == FAT_ATTRIBUTE_LFN)
					continue;
				if (memcmp_simple(entry->Name, shortName, 11) == 0) {
					// Free cluster chain
					uint32_t c = entry->FirstClusterLow |
								 ((uint32_t)entry->FirstClusterHigh << 16);
					while (c >= 2 && c < 0x0FFFFFF8) {
						uint32_t next = fat_next_cluster(c);
						fat_set_cluster(c, 0);
						c = next;
					}
					// Mark entry as deleted
					entry->Name[0] = 0xE5;
					disk_write(lba + s, 1, sector);
					return true;
				}
			}
		}
		cluster = fat_next_cluster(cluster);
	}
	return false;
}

bool FAT_WriteFile(const char *path, const void *data, uint32_t size) {
	// Delete existing file if present, then recreate
	FAT_DeleteEntry(path);
	if (!FAT_CreateFile(path))
		return false;

	if (size == 0)
		return true;

	// Allocate clusters for data
	uint32_t spc = g_Data->BS.BootSector.SectorsPerCluster;
	uint32_t clusterBytes = spc * SECTOR_SIZE;
	uint32_t firstCluster = 0, prevCluster = 0;
	const uint8_t *src = (const uint8_t *)data;
	uint32_t remaining = size;
	uint8_t buf[SECTOR_SIZE];

	while (remaining > 0) {
		uint32_t c = fat_alloc_cluster();
		if (!c)
			return false;

		if (!firstCluster)
			firstCluster = c;
		if (prevCluster)
			fat_set_cluster(prevCluster, c);
		prevCluster = c;

		uint32_t lba = fat_cluster_to_lba(c);
		for (uint32_t s = 0; s < spc && remaining > 0; s++) {
			uint32_t take = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
			memset_simple(buf, 0, SECTOR_SIZE);
			memcpy_simple(buf, src, take);
			disk_write(lba + s, 1, buf);
			src += take;
			remaining -= take;
		}
	}

	// Update directory entry with cluster and size
	const char *slash = 0;
	for (int i = 0; path[i]; i++)
		if (path[i] == '/')
			slash = path + i;

	char dirPath[MAX_PATH] = "";
	char fileName[MAX_PATH] = "";
	if (slash) {
		int dlen = (int)(slash - path);
		memcpy_simple(dirPath, path, dlen);
		dirPath[dlen] = '\0';
		memcpy_simple(fileName, slash + 1, strlen_simple(slash + 1));
	} else {
		memcpy_simple(fileName, path, strlen_simple(path));
	}

	char shortName[12];
	fat_get_short_name(fileName, shortName);

	uint32_t dirCluster = fat_get_dir_cluster(dirPath[0] ? dirPath : NULL);
	uint32_t cluster = dirCluster;

	while (cluster < 0x0FFFFFF8) {
		uint32_t lba = fat_cluster_to_lba(cluster);
		for (uint8_t s = 0; s < spc; s++) {
			disk_read(lba + s, 1, buf);
			for (int e = 0; e < SECTOR_SIZE / 32; e++) {
				FAT_DirectoryEntry *entry =
					(FAT_DirectoryEntry *)(buf + e * 32);
				if (entry->Name[0] == 0x00)
					goto not_found;
				if ((uint8_t)entry->Name[0] == 0xE5)
					continue;
				if (memcmp_simple(entry->Name, shortName, 11) == 0) {
					entry->FirstClusterHigh = (uint16_t)(firstCluster >> 16);
					entry->FirstClusterLow = (uint16_t)(firstCluster);
					entry->Size = size;
					disk_write(lba + s, 1, buf);
					return true;
				}
			}
		}
		cluster = fat_next_cluster(cluster);
	}
not_found:
	return false;
}

uint32_t FAT_GetBytesPerCluster() {
	return g_Data->BS.BootSector.SectorsPerCluster * SECTOR_SIZE;
}

// fat.c
bool FAT_GetDiskInfo(FAT_DiskInfo *info) {
	uint8_t spc = g_Data->BS.BootSector.SectorsPerCluster;
	info->BytesPerCluster = spc * SECTOR_SIZE;
	info->TotalClusters = (g_TotalSectors - g_DataSectionLba) / spc;
	info->FreeClusters = 0;

	// Walk the FAT counting free clusters (cluster 0 entry = FAT32)
	uint32_t total = info->TotalClusters;
	if (total > 65536)
		total = 65536; // cap scan to avoid slowness

	for (uint32_t c = 2; c < total + 2; c++) {
		if (fat_next_cluster(c) == 0)
			info->FreeClusters++;
	}
	return true;
}
