#include "shell.h"
#include "fs/fat.h"
#include "pmm.h"
#include "scheduler.h"
#include <arch/i686/io.h>
#include <arch/i686/keyboard.h>
#include <arch/i686/vga_text.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// ── string helpers (no stdlib) ─────────────────────────────────────────────

static int sh_strlen(const char *s) {
	int n = 0;
	while (s[n])
		n++;
	return n;
}

static int sh_strcmp(const char *a, const char *b) {
	while (*a && *b && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

static int sh_strncmp(const char *a, const char *b, int n) {
	while (n-- && *a && *b && *a == *b) {
		a++;
		b++;
	}
	if (n < 0)
		return 0;
	return (unsigned char)*a - (unsigned char)*b;
}

// skip leading spaces, return pointer to first non-space
static const char *sh_skip(const char *s) {
	while (*s == ' ')
		s++;
	return s;
}

// ── line buffer ────────────────────────────────────────────────────────────

#define LINE_MAX 128

static char g_Line[LINE_MAX];
static int g_LineLen = 0;
static bool g_LineReady = false;

// ── VGA color helpers ──────────────────────────────────────────────────────
// We write directly to the VGA attribute byte so individual commands
// can print in colour without changing the rest of stdio.

extern uint8_t *g_ScreenBuffer;
extern int g_ScreenX, g_ScreenY;
extern const unsigned SCREEN_WIDTH, SCREEN_HEIGHT;

static void set_color(uint8_t color) {
	// paint the attribute byte for the current cursor position
	// (called before printf so subsequent chars get the right color)
	// We store the "pending color" and apply it in putc via a small trick:
	// just set the attribute of the cell we're about to write.
	// Actually the simplest approach: repaint what's already there.
	// Better: export a VGA_setcolor() from vga_text if needed.
	// For now we live with DEFAULT_COLOR and use a prefix character approach.
	(void)color; // stubbed — see note below
}
// Note: full per-command colour would need a VGA_setcolor() helper exported
// from vga_text.c. For this shell all output is in the default colour.
// If you want colour, add:
//   void VGA_setcolor(uint8_t c) { g_CurrentColor = c; }
// and use it in VGA_putchr.

// ── commands ───────────────────────────────────────────────────────────────

static void cmd_help(const char *args) {
	printf("\n");
	printf("UamiOS Shell - available commands:\n");
	printf("\n");

	printf("  System:\n");
	printf("    help        show this message\n");
	printf("    clear       clear the screen\n");
	printf("    version     OS version info\n");
	printf("    fastfetch   system information\n");
	printf("    reboot      restart the system\n");
	printf("    shutdown    power off the system\n");
	printf("\n");

	printf("  Memory:\n");
	printf("    meminfo     physical memory usage\n");
	printf("    pmm_demo    interactive page allocator demo\n");
	printf("\n");

	printf("  Scheduling:\n");
	printf("    utop        show tasks + schedule counts\n");
	printf("    sched_demo  round-robin scheduler demo\n");
	printf("    create <n>  create a new counting task (demo use)\n");
	printf("    kill <id>   kill a task by ID\n");
	printf("\n");

	printf("  Filesystem:\n");
	printf("    ls [path]   list directory contents\n");
	printf("    cat <file>  display file contents\n");
	printf("    touch <f>   create empty file\n");
	printf("    mkdir <d>   create directory\n");
	printf("    rm <name>   delete file\n");
	printf("    rmdir <d>   delete directory\n");
	printf("\n");

	printf("  Other:\n");
	printf("    print <x>   echo text\n");
	printf("    cow         a friendly cow\n");
	printf("\n");
}

static void cmd_clear(const char *args) {
	// Clear by printing 25 blank lines (uses existing VGA scroll)
	for (int i = 0; i < 25; i++)
		printf("\n");
}

static void cmd_version(const char *args) {
	printf("\n");
	printf("  UamiOS v0.1\n");
	printf("  Architecture : x86 (i686), 32-bit protected mode\n");
	printf("  Bootloader   : custom 2-stage (FAT32)\n");
	printf("  Kernel       : monolithic, written in C\n");
	printf("  Build        : debug\n");
	printf("\n");
}

static void cmd_fastfetch(const char *args) {
	uint32_t free_kb = PMM_GetFreePages() * 4;
	uint32_t total_kb = PMM_GetTotalPages() * 4;
	uint32_t used_kb = total_kb - free_kb;

	FAT_DiskInfo disk = {0};
	FAT_GetDiskInfo(&disk);
	uint32_t disk_total_kb = (disk.TotalClusters * disk.BytesPerCluster) / 1024;
	uint32_t disk_free_kb = (disk.FreeClusters * disk.BytesPerCluster) / 1024;
	uint32_t disk_used_kb = disk_total_kb - disk_free_kb;
	uint32_t disk_pct =
		disk_total_kb > 0 ? (disk_used_kb * 100 / disk_total_kb) : 0;

	printf("\n");
	printf("@@@  @@@   @@@@@@   @@@@@@@@@@   @@@   @@@@@@    @@@@@@\n");
	printf("@@@  @@@  @@@@@@@@  @@@@@@@@@@@  @@@  @@@@@@@@  @@@@@@@\n");
	printf("@@!  @@@  @@!  @@@  @@! @@! @@!  @@!  @@!  @@@  !@@\n");
	printf("!@!  @!@  !@!  @!@  !@! !@! !@!  !@!  !@!  @!@  !@!\n");
	printf("@!@  !@!  @!@!@!@!  @!! !!@ @!@  !!@  @!@  !@!  !!@@!!\n");
	printf("!@!  !!!  !!!@!!!!  !@!   ! !@!  !!!  !@!  !!!   !!@!!!\n");
	printf("!!:  !!!  !!:  !!!  !!:     !!:  !!:  !!:  !!!       !:!\n");
	printf(":!:  !:!  :!:  !:!  :!:     :!:  :!:  :!:  !:!      !:!\n");
	printf("::::: ::  ::   :::  :::     ::    ::  ::::: ::  :::: ::\n");
	printf(" : :  :    :   : :   :      :    :     : :  :   :: : :\n");
	printf("\n");
	printf("  OS       : UamiOS v0.2\n");
	printf("  Arch     : i686 (32-bit x86)\n");
	printf("  Mode     : Protected Mode\n");
	printf("  RAM      : %u KB used / %u KB total\n", used_kb, total_kb);
	printf("  Free RAM : %u KB\n", free_kb);
	printf("  Disk     : %u KB used / %u KB total (%u%%)\n", disk_used_kb,
		   disk_total_kb, disk_pct);
	printf("  Free Disk: %u KB\n", disk_free_kb);
	printf("  Shell    : UamiShell v0.2\n");
	printf("  Kernel   : Monolithic C kernel\n");
	printf("  Boot     : Custom 2-stage FAT32 bootloader\n");
}

static void cmd_meminfo(const char *args) {
	uint32_t free_pages = PMM_GetFreePages();
	uint32_t total_pages = PMM_GetTotalPages();
	uint32_t used_pages = total_pages - free_pages;

	uint32_t free_kb = free_pages * 4;
	uint32_t total_kb = total_pages * 4;
	uint32_t used_kb = used_pages * 4;

	// Simple bar: 40 chars wide
	int bar_filled = (total_pages > 0) ? (used_pages * 40 / total_pages) : 0;

	printf("\n");
	printf("  Physical Memory\n");
	printf("  ---------------\n");
	printf("  Total : %u KB  (%u pages)\n", total_kb, total_pages);
	printf("  Used  : %u KB  (%u pages)\n", used_kb, used_pages);
	printf("  Free  : %u KB  (%u pages)\n", free_kb, free_pages);
	printf("  Page size: 4 KB\n");
	printf("\n");
	printf("  [");
	for (int i = 0; i < 40; i++)
		printf(i < bar_filled ? "#" : ".");
	printf("] %u%%\n", total_pages > 0 ? used_pages * 100 / total_pages : 0);
	printf("\n");
}

// utop needs access to the task table — expose a query function from scheduler

static const char *state_name(int state) {
	switch (state) {
	case 0:
		return "READY  ";
	case 1:
		return "RUNNING";
	case 2:
		return "DEAD   ";
	default:
		return "UNKNOWN";
	}
}

// ── shared demo task state ─────────────────────────────────────────────────

static volatile int g_DemoRunning = 0;
static volatile int g_DemoTasks[3] = {-1, -1, -1};

static void demo_task_a() {
	int count = 0;
	while (g_DemoRunning) {
		printf("A%d ", count++);
		// small delay so output is readable, yield on each tick
		volatile int d = 0;
		while (d++ < 200000)
			if (g_NeedSchedule) {
				g_NeedSchedule = 0;
				Scheduler_Yield();
			}
	}
	Scheduler_KillTask(g_DemoTasks[0]);
	for (;;) {
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}
}

static void demo_task_b() {
	int count = 0;
	while (g_DemoRunning) {
		printf("B%d ", count++);
		volatile int d = 0;
		while (d++ < 200000)
			if (g_NeedSchedule) {
				g_NeedSchedule = 0;
				Scheduler_Yield();
			}
	}
	Scheduler_KillTask(g_DemoTasks[1]);
	for (;;) {
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}
}

static void demo_task_c() {
	int count = 0;
	while (g_DemoRunning) {
		printf("C%d ", count++);
		volatile int d = 0;
		while (d++ < 200000)
			if (g_NeedSchedule) {
				g_NeedSchedule = 0;
				Scheduler_Yield();
			}
	}
	Scheduler_KillTask(g_DemoTasks[2]);
	for (;;) {
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}
}
// ── updated utop ───────────────────────────────────────────────────────────

static void print_padded(const char *s, int width) {
	int len = 0;
	while (s[len]) {
		printf("%c", s[len]);
		len++;
	}
	while (len < width) {
		printf(" ");
		len++;
	}
}

static void print_num_padded(uint32_t n, int width) {
	// print number right-aligned in field of `width`
	char buf[12];
	int pos = 0;
	if (n == 0) {
		buf[pos++] = '0';
	} else {
		uint32_t tmp = n;
		while (tmp) {
			buf[pos++] = '0' + tmp % 10;
			tmp /= 10;
		}
	}
	// reverse
	for (int i = 0; i < pos / 2; i++) {
		char t = buf[i];
		buf[i] = buf[pos - 1 - i];
		buf[pos - 1 - i] = t;
	}
	buf[pos] = '\0';
	int pad = width - pos;
	while (pad-- > 0)
		printf(" ");
	for (int i = 0; i < pos; i++)
		printf("%c", buf[i]);
}

static void cmd_utop(const char *args) {
	int count = Scheduler_GetTaskCount();

	printf("\n");
	printf("  UamiOS Task Manager (utop)\n");
	printf("  --------------------------\n");
	printf("  Tasks: %d running / %d max slots\n", count, MAX_TASKS);
	printf("  Scheduler: Round-Robin, quantum = 500 timer ticks (~5s)\n");
	printf("  Timer: 100 Hz\n");
	printf("\n");

	// Header
	printf("  ID  ");
	printf("Name             ");
	printf("State    ");
	printf("Scheduled\n");
	printf("  --- ");
	printf("---------------- ");
	printf("-------- ");
	printf("---------\n");

	for (int i = 0; i < count; i++) {
		char name[33];
		int state;
		uint32_t scount;
		Scheduler_GetTask(i, name, &state, &scount);

		printf("  ");
		print_num_padded(i, 3);
		printf(" ");
		print_padded(name, 16);
		printf(" ");
		print_padded(state_name(state), 8);
		printf(" ");
		print_num_padded(scount, 9);
		printf("\n");
	}

	printf("\n");

	// Show per-task explanation
	printf("  Legend:\n");
	printf("    READY   = waiting for CPU turn\n");
	printf("    RUNNING = currently executing\n");
	printf("    DEAD    = terminated, slot freed\n");
	printf("    Scheduled = how many times this task got the CPU\n");
	printf("\n");

	// Show scheduling fairness if more than 1 non-idle task
	if (count > 2) {
		printf("  Scheduling fairness (non-idle tasks):\n");
		uint32_t total_switches = 0;
		for (int i = 1; i < count; i++) {
			char name[33];
			int state;
			uint32_t sc;
			Scheduler_GetTask(i, name, &state, &sc);
			total_switches += sc;
		}
		for (int i = 1; i < count; i++) {
			char name[33];
			int state;
			uint32_t sc;
			Scheduler_GetTask(i, name, &state, &sc);
			uint32_t pct = total_switches > 0 ? sc * 100 / total_switches : 0;
			printf("    ");
			print_padded(name, 16);
			printf(" got ");
			print_num_padded(pct, 3);
			printf("%% of CPU time (");
			print_num_padded(sc, 0);
			printf(" switches)\n");
		}
		printf("\n");
	}
}

// ── sched_demo ─────────────────────────────────────────────────────────────

// Replace cmd_sched_demo with this:
static void cmd_sched_demo(const char *args) {
	if (g_DemoRunning) {
		printf("\nDemo already running. Type 'kill' to stop it first.\n");
		return;
	}

	int extra = 0; // extra user-created tasks will be counted separately
	int base = Scheduler_GetTaskCount();

	printf("\n");
	printf("Round-Robin Scheduler Demo\n");
	printf("--------------------------\n");
	printf("Tasks A, B, C will take turns. Each gets one quantum\n");
	printf("(~5 timer ticks) before the scheduler switches.\n");
	printf("Watching the output shows the round-robin order.\n");
	printf("Auto-stops after 15 seconds.\n\n");

	g_DemoRunning = 1;
	Scheduler_CreateTask("demo_A", demo_task_a);
	Scheduler_CreateTask("demo_B", demo_task_b);
	Scheduler_CreateTask("demo_C", demo_task_c);
	g_DemoTasks[0] = base;
	g_DemoTasks[1] = base + 1;
	g_DemoTasks[2] = base + 2;

	// Timer runs at 100 Hz so 15 seconds = 1500 ticks
	uint32_t start = Scheduler_GetTicks();
	uint32_t last_print = start;
	int seconds = 0;

	while (1) {
		uint32_t now = Scheduler_GetTicks();

		// Stop after 15 seconds
		if (now - start >= 1500) {
			g_DemoRunning = 0;
			printf("\n\n[15 seconds elapsed - demo stopped]\n");
			break;
		}

		// Print a separator line every second so output is readable
		if (now - last_print >= 100) {
			last_print = now;
			seconds++;
			printf("\n--- %d s | A scheduled %u | B scheduled %u | C scheduled "
				   "%u ---\n",
				   seconds, Scheduler_GetScheduleCount(base),
				   Scheduler_GetScheduleCount(base + 1),
				   Scheduler_GetScheduleCount(base + 2));
		}

		// Allow Enter to stop early
		if (Keyboard_HasChar()) {
			char c = Keyboard_GetChar();
			if (c == '\n' || c == '\r') {
				g_DemoRunning = 0;
				printf("\n\n[Demo stopped by user]\n");
				break;
			}
		}

		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}

	// Kill demo tasks
	for (int i = 0; i < 3; i++)
		Scheduler_KillTask(g_DemoTasks[i]);
}

// ── create ─────────────────────────────────────────────────────────────────

// A generic counting task — name is baked in at creation via a slot
static volatile int g_UserTaskRunning[MAX_TASKS] = {0};

static void user_task_fn_0() {
	int c = 0;
	while (g_UserTaskRunning[0]) {
		printf("[T0:%d]", c++);
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}
	for (;;) {
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}
}
static void user_task_fn_1() {
	int c = 0;
	while (g_UserTaskRunning[1]) {
		printf("[T1:%d]", c++);
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}
	for (;;) {
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}
}
static void user_task_fn_2() {
	int c = 0;
	while (g_UserTaskRunning[2]) {
		printf("[T2:%d]", c++);
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}
	for (;;) {
		if (g_NeedSchedule) {
			g_NeedSchedule = 0;
			Scheduler_Yield();
		}
	}
}

typedef void (*TaskFn)();
static const TaskFn g_UserTaskFns[] = {user_task_fn_0, user_task_fn_1,
									   user_task_fn_2};
static int g_UserTaskSlot = 0;

static void cmd_create(const char *args) {
	const char *name = sh_skip(args);
	if (*name == '\0')
		name = "task";

	printf("\nNote: 'create' adds a task to the next sched_demo run.\n");
	printf("      (live task creation during demo not yet supported)\n");
	printf("      Run 'sched_demo' to see all tasks interleave.\n");

	if (Scheduler_GetTaskCount() >= MAX_TASKS) {
		printf("      Max tasks reached.\n\n");
		return;
	}

	// For now just inform — the demo always creates A/B/C
	// You can extend this to register a 4th demo task if you want
	printf("      Currently demo uses fixed tasks A, B, C.\n\n");
}

// ── kill ───────────────────────────────────────────────────────────────────

static void cmd_kill(const char *args) {
	const char *p = sh_skip(args);
	if (*p == '\0') {
		printf("\nUsage: kill <task_id>\n");
		printf("Use 'utop' to see task IDs.\n");
		return;
	}

	// parse integer
	int id = 0;
	while (*p >= '0' && *p <= '9') {
		id = id * 10 + (*p - '0');
		p++;
	}

	if (id == 0) {
		printf("\nCannot kill the idle task (id 0).\n");
		return;
	}
	if (id >= Scheduler_GetTaskCount()) {
		printf("\nNo task with id %d.\n", id);
		return;
	}

	// Stop user task loops
	if (id < 3)
		g_UserTaskRunning[id] = 0;
	g_DemoRunning = 0;

	Scheduler_KillTask(id);
	printf("\nKilled task %d.\n", id);
}

// ── pmm_demo ───────────────────────────────────────────────────────────────

static void cmd_pmm_demo(const char *args) {
	printf("\n");
	printf("Physical Memory Manager Demo\n");
	printf("----------------------------\n");
	printf("Page size: 4096 bytes (4 KB)\n\n");

	uint32_t before = PMM_GetFreePages() * 4;
	printf("Free before allocation: %u KB\n\n", before);

	void *pages[5];
	for (int i = 0; i < 5; i++) {
		pages[i] = PMM_AllocPage();
		printf("  Allocated page %d: 0x%x  (+4KB from previous)\n", i,
			   (uint32_t)pages[i]);
	}

	uint32_t during = PMM_GetFreePages() * 4;
	printf("\nFree after 5 allocations: %u KB (decreased by %u KB)\n\n", during,
		   before - during);

	for (int i = 0; i < 5; i++) {
		PMM_FreePage(pages[i]);
		printf("  Freed page %d: 0x%x\n", i, (uint32_t)pages[i]);
	}

	uint32_t after = PMM_GetFreePages() * 4;
	printf("\nFree after freeing all: %u KB (restored)\n", after);
	printf("\nBitmap allocator: pages are found by scanning bits,\n");
	printf("each bit = one 4KB page. 0=free, 1=used.\n\n");
}

static void cmd_print(const char *args) {
	const char *p = sh_skip(args);
	if (*p == '\0') {
		printf("\n");
		return;
	}

	// Find > or >> operator
	const char *gt = 0;
	bool append = false;

	for (int i = 0; p[i]; i++) {
		if (p[i] == '>' && p[i + 1] == '>') {
			gt = p + i;
			append = true;
			break;
		} else if (p[i] == '>') {
			gt = p + i;
			append = false;
			break;
		}
	}

	if (!gt) {
		// No redirection — just print
		printf("\n%s\n", p);
		return;
	}

	// Split text and filename
	char text[LINE_MAX] = "";
	int tlen = (int)(gt - p);

	// Trim trailing spaces
	while (tlen > 0 && p[tlen - 1] == ' ')
		tlen--;

	// Strip optional surrounding quotes
	int tstart = 0;
	if (tlen >= 2 && (p[0] == '"' || p[0] == '\'')) {
		tstart = 1;
		tlen--;
		if (tlen > 0 &&
			(p[tstart + tlen - 1] == '"' || p[tstart + tlen - 1] == '\''))
			tlen--;
	}

	for (int i = 0; i < tlen && i < LINE_MAX - 1; i++)
		text[i] = p[tstart + i];
	text[tlen < LINE_MAX - 1 ? tlen : LINE_MAX - 1] = '\0';

	// Filename
	const char *fname = sh_skip(gt + (append ? 2 : 1));
	if (*fname == '\0') {
		printf("\nprint: missing filename after >\n");
		return;
	}

	if (append) {
		// Read existing content
		char existing[512] = "";
		int elen = 0;

		FAT_File *f = FAT_Open(fname);
		if (f) {
			elen = FAT_Read(f, sizeof(existing) - 1, existing);
			existing[elen] = '\0';
			FAT_Close(f);
		}

		// Combine
		char combined[512 + LINE_MAX];
		int clen = 0;

		for (int i = 0; i < elen; i++)
			combined[clen++] = existing[i];

		if (elen > 0 && existing[elen - 1] != '\n')
			combined[clen++] = '\n';

		for (int i = 0; text[i] && clen < (int)sizeof(combined) - 2; i++)
			combined[clen++] = text[i];

		combined[clen++] = '\n';
		combined[clen] = '\0';

		if (FAT_WriteFile(fname, combined, clen))
			printf("\nAppended to %s\n", fname);
		else
			printf("\nFailed to write %s\n", fname);
	} else {
		// Overwrite
		char buf[LINE_MAX + 2];
		int blen = 0;

		for (int i = 0; text[i]; i++)
			buf[blen++] = text[i];

		buf[blen++] = '\n';
		buf[blen] = '\0';

		if (FAT_WriteFile(fname, buf, blen))
			printf("\nWrote to %s\n", fname);
		else
			printf("\nFailed to write %s\n", fname);
	}
}

static void cmd_cow(const char *args) {
	// printf("\n");
	printf("        ^__^\n");
	printf("        (oo)\\_______\n");
	printf("        (__)\\       )\\/\\\n");
	printf("            ||----w |\n");
	printf("            ||     ||\n");
	// printf("\n");
}

static void cmd_reboot(const char *args) {
	printf("\nRebooting...\n");
	// Pulse the keyboard controller reset line
	i686_outb(0x64, 0xFE);
	// If that didn't work, triple fault
	for (;;)
		i686_outb(0x64, 0xFE);
}

static void cmd_shutdown(const char *args) {
	printf("\nShutting down...\n");
	// QEMU ACPI shutdown port
	i686_outb(0x604, 0x00);
	i686_outw(0x604, 0x2000);
	// Bochs/older QEMU fallback
	i686_outw(0xB004, 0x2000);
	for (;;)
		;
}

static void ls_callback(const char *name, bool isDir, uint32_t size) {
	if (isDir)
		printf("  [DIR]  %s\n", name);
	else
		printf("  [FILE] %-20s  %u bytes\n", name, size);
}

static void cmd_ls(const char *args) {
	const char *path = sh_skip(args);
	if (*path == '\0')
		path = "/";
	printf("\nContents of %s:\n", path);
	if (!FAT_ListDir(path, ls_callback))
		printf("  (no such directory)\n");
	printf("\n");
}

static void cmd_touch(const char *args) {
	const char *name = sh_skip(args);
	if (*name == '\0') {
		printf("\nUsage: touch <filename>\n");
		return;
	}
	if (FAT_CreateFile(name))
		printf("\nCreated: %s\n", name);
	else
		printf("\nFailed to create: %s\n", name);
}

static void cmd_mkdir(const char *args) {
	const char *name = sh_skip(args);
	if (*name == '\0') {
		printf("\nUsage: mkdir <dirname>\n");
		return;
	}
	if (FAT_CreateDir(name))
		printf("\nCreated directory: %s\n", name);
	else
		printf("\nFailed to create directory: %s\n", name);
}

static void cmd_rm(const char *args) {
	const char *name = sh_skip(args);
	if (*name == '\0') {
		printf("\nUsage: rm <filename>\n");
		return;
	}
	if (FAT_DeleteEntry(name))
		printf("\nDeleted: %s\n", name);
	else
		printf("\nNot found: %s\n", name);
}

static void cmd_rmdir(const char *args) {
	// rmdir is same as rm for us — just deletes the entry
	cmd_rm(args);
}

static void cmd_cat(const char *args) {
	const char *name = sh_skip(args);
	if (*name == '\0') {
		printf("\nUsage: cat <filename>\n");
		return;
	}

	FAT_File *f = FAT_Open(name);
	if (!f) {
		printf("\nNot found: %s\n", name);
		return;
	}

	printf("\n");
	uint8_t buf[64];
	uint32_t n;
	while ((n = FAT_Read(f, sizeof(buf) - 1, buf)) > 0) {
		buf[n] = '\0';
		printf("%s", (char *)buf);
	}
	printf("\n");
	FAT_Close(f);
}

static void cmd_df(const char *args) {
	// Count used clusters by walking the FAT
	// We expose a helper from fat.c for this
	printf("\nDisk usage is shown per file with 'ls'.\n");
	printf("Each file cluster = %u bytes on disk.\n", FAT_GetBytesPerCluster());
	printf("\n");
}
// ── command dispatch ───────────────────────────────────────────────────────

typedef struct {
	const char *name;
	void (*fn)(const char *args);
} Command;

static const Command g_Commands[] = {
	{"help", cmd_help},
	{"clear", cmd_clear},
	{"version", cmd_version},
	{"fastfetch", cmd_fastfetch},

	// Memory
	{"meminfo", cmd_meminfo},
	{"pmm_demo", cmd_pmm_demo},

	// Scheduling
	{"utop", cmd_utop},
	{"sched_demo", cmd_sched_demo},
	{"create", cmd_create},
	{"kill", cmd_kill},

	// Filesystem
	{"ls", cmd_ls},
	{"cat", cmd_cat},
	{"touch", cmd_touch},
	{"mkdir", cmd_mkdir},
	{"rm", cmd_rm},
	{"rmdir", cmd_rmdir},
	{"df", cmd_df}, // ← missing before

	// Other
	{"print", cmd_print},
	{"cow", cmd_cow},
	{"reboot", cmd_reboot},
	{"shutdown", cmd_shutdown},
};

#define NUM_COMMANDS (int)(sizeof(g_Commands) / sizeof(g_Commands[0]))

static void dispatch(const char *line) {
	const char *p = sh_skip(line);
	if (*p == '\0')
		return;

	// Find end of command word
	int cmdlen = 0;
	while (p[cmdlen] && p[cmdlen] != ' ')
		cmdlen++;

	// Find args (everything after the command word)
	const char *args = sh_skip(p + cmdlen);

	for (int i = 0; i < NUM_COMMANDS; i++) {
		int nlen = sh_strlen(g_Commands[i].name);
		if (cmdlen == nlen && sh_strncmp(p, g_Commands[i].name, cmdlen) == 0) {
			g_Commands[i].fn(args);
			return;
		}
	}

	printf("\nunknown command: ");
	for (int i = 0; i < cmdlen; i++)
		printf("%c", p[i]);
	printf(" (type 'help' for commands)\n");
}

// ── shell main loop ────────────────────────────────────────────────────────

void Shell_Initialize() {
	g_LineLen = 0;
	g_LineReady = false;
}

static void print_prompt() { printf("\nuamios> "); }

void Shell_Run() {
	print_prompt();

	for (;;) {
		// Yield while waiting for input
		while (!Keyboard_HasChar()) {
			if (g_NeedSchedule) {
				g_NeedSchedule = 0;
				Scheduler_Yield();
			}
		}

		char c = Keyboard_GetChar();
		if (c == KEY_PAGEUP) {
			VGA_ScrollUp(5);
			// don't add to line buffer, don't echo
		} else if (c == KEY_PAGEDOWN) {
			VGA_ScrollDown(5);
		} else if (c == '\n' || c == '\r') {
			g_Line[g_LineLen] = '\0';
			printf("\n");
			dispatch(g_Line);
			g_LineLen = 0;
			print_prompt();
		} else if (c == '\b') {
			// Backspace
			if (g_LineLen > 0) {
				g_LineLen--;
				// Erase character on screen
				if (g_ScreenX > 0) {
					g_ScreenX--;
				}
				// Use VGA directly to blank the character
				extern void VGA_putchr(int x, int y, char c);
				VGA_putchr(g_ScreenX, g_ScreenY, ' ');
				extern void VGA_setcursor(int x, int y);
				VGA_setcursor(g_ScreenX, g_ScreenY);
			}
		} else if (g_LineLen < LINE_MAX - 1) {
			g_Line[g_LineLen++] = c;
			// character already echoed by keyboard handler
		}
	}
}
