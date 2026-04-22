#include <arch/i686/io.h>
#include <stdbool.h>
#include <stdint.h>

const unsigned SCREEN_WIDTH = 80;
const unsigned SCREEN_HEIGHT = 25;
const uint8_t DEFAULT_COLOR = 0x7;

uint8_t *g_ScreenBuffer = (uint8_t *)0xB8000;
int g_ScreenX = 0, g_ScreenY = 0;

// ── scrollback ─────────────────────────────────────────────────────────────
#define SCROLLBACK_LINES 200

// Each line stores 80 chars + 80 attrs interleaved (char,attr,char,attr...)
static uint8_t g_SBuf[SCROLLBACK_LINES][80 * 2];

// g_SBHead: the scrollback line index that corresponds to screen row 0
// when we are in live view. Advances every time the screen scrolls up.
static int g_SBHead = 0;	 // index of oldest on-screen line in scrollback
static int g_SBCount = 0;	 // total lines stored (capped at SCROLLBACK_LINES)
static int g_ViewOffset = 0; // 0=live, positive=scrolled up

// Get a pointer to a cell in the scrollback buffer
static inline uint8_t *sb_cell(int sbLine, int x) {
	return &g_SBuf[((sbLine) % SCROLLBACK_LINES + SCROLLBACK_LINES) %
				   SCROLLBACK_LINES][x * 2];
}

// Copy current VGA screen into the scrollback buffer at the right position
static void sb_sync_from_vga() {
	// The on-screen lines map to scrollback lines [g_SBHead .. g_SBHead+24]
	for (int row = 0; row < (int)SCREEN_HEIGHT; row++) {
		int sbLine = g_SBHead + row;
		for (int col = 0; col < (int)SCREEN_WIDTH; col++) {
			uint8_t *cell = sb_cell(sbLine, col);
			cell[0] = g_ScreenBuffer[2 * (row * SCREEN_WIDTH + col)];
			cell[1] = g_ScreenBuffer[2 * (row * SCREEN_WIDTH + col) + 1];
		}
	}
}

// Render scrollback buffer to VGA at the current view offset
static void sb_render() {
	// live bottom line in scrollback = g_SBHead + SCREEN_HEIGHT - 1
	// when offset=0 we show lines [g_SBHead .. g_SBHead+24]
	// when offset=N we show lines [g_SBHead-N .. g_SBHead-N+24]
	int topSB = g_SBHead - g_ViewOffset;

	for (int row = 0; row < (int)SCREEN_HEIGHT; row++) {
		int sbLine = topSB + row;
		for (int col = 0; col < (int)SCREEN_WIDTH; col++) {
			uint8_t ch = 0;
			uint8_t attr = DEFAULT_COLOR;
			// Only show lines that have been written
			if (sbLine >= 0 && sbLine < g_SBCount + (int)SCREEN_HEIGHT) {
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
	g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x)] = c;
	// Keep scrollback in sync
	uint8_t *cell = sb_cell(g_SBHead + y, x);
	cell[0] = (uint8_t)c;
	cell[1] = DEFAULT_COLOR;
}

void VGA_putcolor(int x, int y, uint8_t color) {
	g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x) + 1] = color;
	uint8_t *cell = sb_cell(g_SBHead + y, x);
	cell[1] = color;
}

char VGA_getchr(int x, int y) {
	return g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x)];
}

uint8_t VGA_getcolor(int x, int y) {
	return g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x) + 1];
}

void VGA_setcursor(int x, int y) {
	if (g_ViewOffset != 0) {
		// Hide cursor while scrolled back
		i686_outb(0x3D4, 0x0A);
		i686_outb(0x3D5, 0x20);
		return;
	}
	int pos = y * SCREEN_WIDTH + x;
	i686_outb(0x3D4, 0x0F);
	i686_outb(0x3D5, (uint8_t)(pos & 0xFF));
	i686_outb(0x3D4, 0x0E);
	i686_outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void VGA_clrscr() {
	for (int y = 0; y < (int)SCREEN_HEIGHT; y++)
		for (int x = 0; x < (int)SCREEN_WIDTH; x++) {
			g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x)] = 0;
			g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x) + 1] = DEFAULT_COLOR;
		}
	g_ScreenX = 0;
	g_ScreenY = 0;
	g_ViewOffset = 0;
	g_SBHead = 0;
	g_SBCount = 0;
	VGA_setcursor(0, 0);
}

// Called when new content pushes screen up by `lines` rows
void VGA_scrollback(int lines) {
	// Scroll VGA content up
	for (int y = lines; y < (int)SCREEN_HEIGHT; y++)
		for (int x = 0; x < (int)SCREEN_WIDTH; x++) {
			g_ScreenBuffer[2 * ((y - lines) * SCREEN_WIDTH + x)] =
				g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x)];
			g_ScreenBuffer[2 * ((y - lines) * SCREEN_WIDTH + x) + 1] =
				g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x) + 1];
		}

	// Clear new blank rows at bottom
	for (int y = SCREEN_HEIGHT - lines; y < (int)SCREEN_HEIGHT; y++)
		for (int x = 0; x < (int)SCREEN_WIDTH; x++) {
			g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x)] = 0;
			g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x) + 1] = DEFAULT_COLOR;
		}

	g_ScreenY -= lines;

	// Advance scrollback head
	g_SBHead += lines;
	g_SBCount += lines;
	if (g_SBCount > SCROLLBACK_LINES - (int)SCREEN_HEIGHT)
		g_SBCount = SCROLLBACK_LINES - (int)SCREEN_HEIGHT;

	// Clear the new scrollback lines (they correspond to the blank rows)
	for (int l = 0; l < lines; l++) {
		int sbLine = g_SBHead + (SCREEN_HEIGHT - lines + l);
		for (int x = 0; x < (int)SCREEN_WIDTH; x++) {
			uint8_t *cell = sb_cell(sbLine, x);
			cell[0] = 0;
			cell[1] = DEFAULT_COLOR;
		}
	}
}

void VGA_putc(char c) {
	// Any new output snaps back to live view
	if (g_ViewOffset != 0) {
		g_ViewOffset = 0;
		sb_render();
	}

	switch (c) {
	case '\n':
		g_ScreenX = 0;
		g_ScreenY++;
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
	int maxOffset = g_SBCount;
	if (maxOffset <= 0)
		return;

	g_ViewOffset += lines;
	if (g_ViewOffset > maxOffset)
		g_ViewOffset = maxOffset;

	sb_render();
	VGA_setcursor(g_ScreenX, g_ScreenY);
}

void VGA_ScrollDown(int lines) {
	g_ViewOffset -= lines;
	if (g_ViewOffset < 0)
		g_ViewOffset = 0;

	if (g_ViewOffset == 0) {
		// Restore live VGA content directly from screen buffer
		// (VGA buffer is always correct, just re-show it)
		// Nothing to do — sb_render at offset 0 should show live content
		// but to be safe, copy live screen back from our shadow in scrollback
		for (int y = 0; y < (int)SCREEN_HEIGHT; y++)
			for (int x = 0; x < (int)SCREEN_WIDTH; x++) {
				uint8_t *cell = sb_cell(g_SBHead + y, x);
				g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x)] = cell[0];
				g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x) + 1] = cell[1];
			}
	} else {
		sb_render();
	}

	VGA_setcursor(g_ScreenX, g_ScreenY);
}
