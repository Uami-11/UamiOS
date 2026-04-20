#include "shell.h"
#include "pmm.h"
#include "scheduler.h"
#include <arch/i686/io.h>
#include <arch/i686/keyboard.h>
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
	printf("  help       - show this message\n");
	printf("  clear      - clear the screen\n");
	printf("  version    - OS version info\n");
	printf("  fastfetch  - system information\n");
	printf("  meminfo    - physical memory usage\n");
	printf("  utop       - show running tasks\n");
	printf("  print <x>  - echo text to screen\n");
	printf("  cow        - a friendly cow\n");
	printf("  reboot     - restart the system\n");
	printf("  shutdown   - power off the system\n");
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
	printf("  OS       : UamiOS v0.1\n");
	printf("  Arch     : i686 (32-bit x86)\n");
	printf("  Mode     : Protected Mode\n");
	printf("  Memory   : %u KB used / %u KB total\n", used_kb, total_kb);
	printf("  Free RAM : %u KB\n", free_kb);
	printf("  Shell    : UamiShell v0.1\n");
	printf("  Kernel   : Monolithic C kernel\n");
	printf("  Boot     : Custom 2-stage FAT32 bootloader\n");
	printf("\n");
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
extern int Scheduler_GetTaskCount();
extern void Scheduler_GetTask(int idx, char *nameOut, int *stateOut);

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

static void cmd_utop(const char *args) {
	int count = Scheduler_GetTaskCount();
	printf("\n");
	printf("  %-4s %-16s %-10s\n", "ID", "NAME", "STATE");
	printf("  ---- ---------------- ----------\n");
	for (int i = 0; i < count; i++) {
		char name[33];
		int state;
		Scheduler_GetTask(i, name, &state);
		printf("  %-4d %-16s %s\n", i, name, state_name(state));
	}
	printf("\n");
}

static void cmd_print(const char *args) {
	const char *text = sh_skip(args);
	if (*text == '\0') {
		printf("\n");
		return;
	}
	printf("\n%s\n", text);
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

// ── command dispatch ───────────────────────────────────────────────────────

typedef struct {
	const char *name;
	void (*fn)(const char *args);
} Command;

static const Command g_Commands[] = {
	{"help", cmd_help},		  {"clear", cmd_clear},
	{"version", cmd_version}, {"fastfetch", cmd_fastfetch},
	{"meminfo", cmd_meminfo}, {"utop", cmd_utop},
	{"print", cmd_print},	  {"cow", cmd_cow},
	{"reboot", cmd_reboot},	  {"shutdown", cmd_shutdown},
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

		if (c == '\n' || c == '\r') {
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
