#include <arch/i686/io.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

const unsigned SCREEN_WIDTH = 80;
const unsigned SCREEN_HEIGHT = 25;
const uint8_t DEFAULT_COLOR = 0x7;

uint8_t *g_ScreenBuffer = (uint8_t *)0xB8000;
int g_ScreenX = 0, g_ScreenY = 0;

// ── scrollback buffer ──────────────────────────────────────────────────────
// Stores every line ever written. Each cell = char + attribute (2 bytes).
// g_TotalLines: how many lines have been committed to the buffer.
// g_ViewOffset: how many lines above the bottom we are currently viewing.
//               0 = live view (bottom), positive = scrolled up.

#define SCROLLBACK_LINES 200

static uint8_t g_SBuf[SCROLLBACK_LINES][80 * 2];
static int g_TotalLines = 1; // line 0 starts empty
static int g_ViewOffset = 0;

// Return pointer to a cell in the scrollback buffer
static uint8_t *sb_cell(int line, int x) {
	return &g_SBuf[line % SCROLLBACK_LINES][x * 2];
}

// Commit the current screen line to the scrollback buffer
// (called when we move to a new line)
static void sb_commit_line(int screenY) {
	// The scrollback line that corresponds to screenY is:
	int sbLine = g_TotalLines - (g_ScreenY - screenY) - 1;
	// We just write all cells of the current logical line
	(void)sbLine; // writing happens in putchr below
}

// Flush from scrollback buffer to VGA memory based on current view offset
static void sb_render() {
	// Which scrollback line maps to screen row 0?
	int topLine = g_TotalLines - SCREEN_HEIGHT - g_ViewOffset;

	for (int row = 0; row < (int)SCREEN_HEIGHT; row++) {
		int sbLine = topLine + row;
		for (int col = 0; col < (int)SCREEN_WIDTH; col++) {
			uint8_t ch = 0;
			uint8_t attr = DEFAULT_COLOR;
			if (sbLine >= 0 && sbLine < g_TotalLines) {
				uint8_t *cell = sb_cell(sbLine, col);
				ch = cell[0];
				attr = cell[1];
			}
			g_ScreenBuffer[2 * (row * SCREEN_WIDTH + col)] = ch;
			g_ScreenBuffer[2 * (row * SCREEN_WIDTH + col) + 1] = attr;
		}
	}
}

// ── basic VGA helpers ──────────────────────────────────────────────────────

void VGA_putchr(int x, int y, char c) {
	// Write to VGA and to scrollback
	g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x)] = c;

	// Map screen y to scrollback line
	int sbLine = g_TotalLines - SCREEN_HEIGHT + y;
	if (sbLine >= 0) {
		uint8_t *cell = sb_cell(sbLine, x);
		cell[0] = (uint8_t)c;
		cell[1] = DEFAULT_COLOR;
	}
}

void VGA_putcolor(int x, int y, uint8_t color) {
	g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x) + 1] = color;
}

char VGA_getchr(int x, int y) {
	return g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x)];
}

uint8_t VGA_getcolor(int x, int y) {
	return g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x) + 1];
}

void VGA_setcursor(int x, int y) {
	// Only show cursor when not scrolled back
	int pos = (g_ViewOffset == 0) ? y * SCREEN_WIDTH + x : 0xFFFF;
	i686_outb(0x3D4, 0x0F);
	i686_outb(0x3D5, (uint8_t)(pos & 0xFF));
	i686_outb(0x3D4, 0x0E);
	i686_outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void VGA_clrscr() {
	for (int y = 0; y < (int)SCREEN_HEIGHT; y++)
		for (int x = 0; x < (int)SCREEN_WIDTH; x++) {
			VGA_putchr(x, y, '\0');
			VGA_putcolor(x, y, DEFAULT_COLOR);
		}
	g_ScreenX = 0;
	g_ScreenY = 0;
	g_ViewOffset = 0;
	VGA_setcursor(g_ScreenX, g_ScreenY);
}

// Called when output scrolls the screen up by `lines` rows
void VGA_scrollback(int lines) {
	// Shift screen content up
	for (int y = lines; y < (int)SCREEN_HEIGHT; y++)
		for (int x = 0; x < (int)SCREEN_WIDTH; x++) {
			VGA_putchr(x, y - lines, VGA_getchr(x, y));
			VGA_putcolor(x, y - lines, VGA_getcolor(x, y));
		}

	// Clear new blank lines at bottom
	for (int y = SCREEN_HEIGHT - lines; y < (int)SCREEN_HEIGHT; y++)
		for (int x = 0; x < (int)SCREEN_WIDTH; x++) {
			VGA_putchr(x, y, '\0');
			VGA_putcolor(x, y, DEFAULT_COLOR);
		}

	g_ScreenY -= lines;

	// Advance the scrollback total line count
	g_TotalLines += lines;
	if (g_TotalLines > SCROLLBACK_LINES)
		g_TotalLines = SCROLLBACK_LINES;

	// Clear the new scrollback lines
	for (int l = 0; l < lines; l++) {
		int sbLine = g_TotalLines - lines + l;
		for (int x = 0; x < (int)SCREEN_WIDTH; x++) {
			uint8_t *cell = sb_cell(sbLine, x);
			cell[0] = 0;
			cell[1] = DEFAULT_COLOR;
		}
	}
}

void VGA_putc(char c) {
	// If scrolled back, jump to live view on any output
	if (g_ViewOffset != 0) {
		g_ViewOffset = 0;
		sb_render();
	}

	switch (c) {
	case '\n':
		g_ScreenX = 0;
		g_ScreenY++;
		// Commit new blank line to scrollback
		if (g_TotalLines < SCROLLBACK_LINES) {
			// clear the new line in scrollback
			int sbLine = g_TotalLines;
			for (int x = 0; x < (int)SCREEN_WIDTH; x++) {
				uint8_t *cell = sb_cell(sbLine, x);
				cell[0] = 0;
				cell[1] = DEFAULT_COLOR;
			}
			g_TotalLines++;
		}
		break;

	case '\t':
		for (int i = 0; i < 4 - (g_ScreenX % 4); i++)
			VGA_putc(' ');
		break;

	case '\r':
		g_ScreenX = 0;
		break;

	default:
		VGA_putchr(g_ScreenX, g_ScreenY, c);
		g_ScreenX++;
		break;
	}

	if (g_ScreenX >= (int)SCREEN_WIDTH) {
		g_ScreenY++;
		g_ScreenX = 0;
	}
	if (g_ScreenY >= (int)SCREEN_HEIGHT)
		VGA_scrollback(1);

	VGA_setcursor(g_ScreenX, g_ScreenY);
}

// ── scrollback navigation ──────────────────────────────────────────────────

void VGA_ScrollUp(int lines) {
	int maxScroll = g_TotalLines - SCREEN_HEIGHT;
	if (maxScroll < 0)
		maxScroll = 0;

	g_ViewOffset += lines;
	if (g_ViewOffset > maxScroll)
		g_ViewOffset = maxScroll;

	sb_render();
	VGA_setcursor(g_ScreenX, g_ScreenY);
}

void VGA_ScrollDown(int lines) {
	g_ViewOffset -= lines;
	if (g_ViewOffset < 0)
		g_ViewOffset = 0;

	sb_render();
	VGA_setcursor(g_ScreenX, g_ScreenY);
}
