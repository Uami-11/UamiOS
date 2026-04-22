#include "keyboard.h"
#include "io.h"
#include "irq.h"
#include <stdio.h>

#define KEYBOARD_DATA_PORT 0x60

// Scancode -> ASCII (lowercase)
static const char g_ScanToAscii[] = {
	0,	 0,	  '1', '2', '3', '4', '5', '6', '7', '8', '9',	'0', '-', '=',	0,
	0,	 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',	'[', ']', '\n', 0,
	'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,	  '\\', 'z',
	'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,	  '*',	0,	 ' '};

// Scancode -> ASCII (shifted)
static const char g_ScanToAsciiShift[] = {
	0,	 0,	  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',  0,
	0,	 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
	'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,	 '|',  'Z',
	'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,	  '*', 0,	' '};

#define SCANCODE_LSHIFT 0x2A
#define SCANCODE_RSHIFT 0x36
#define SCANCODE_LSHIFT_REL 0xAA
#define SCANCODE_RSHIFT_REL 0xB6
#define SCANCODE_BACKSPACE 0x0E
#define SCANCODE_CAPS 0x3A

// Add these defines at the top with the other scancodes:
#define SCANCODE_PAGEUP 0x49
#define SCANCODE_PAGEDOWN 0x51

// Special non-ASCII sentinel values put into the key buffer
#define KEY_PAGEUP 0x01
#define KEY_PAGEDOWN 0x02

static bool g_ShiftHeld = false;
static bool g_CapsLock = false;
static bool g_ExtendedKey = false;

// Shell input buffer — filled by keyboard, consumed by shell
#define KEY_BUFFER_SIZE 256
static char g_KeyBuffer[KEY_BUFFER_SIZE];
static volatile int g_KeyHead = 0;
static volatile int g_KeyTail = 0;

void Keyboard_PutChar(char c) {
	int next = (g_KeyTail + 1) % KEY_BUFFER_SIZE;
	if (next != g_KeyHead) {
		g_KeyBuffer[g_KeyTail] = c;
		g_KeyTail = next;
	}
}

// Returns 0 if buffer empty
char Keyboard_GetChar() {
	if (g_KeyHead == g_KeyTail)
		return 0;
	char c = g_KeyBuffer[g_KeyHead];
	g_KeyHead = (g_KeyHead + 1) % KEY_BUFFER_SIZE;
	return c;
}

int Keyboard_HasChar() { return g_KeyHead != g_KeyTail; }

static void keyboard_handler(Registers *regs) {
	uint8_t scancode = i686_inb(KEYBOARD_DATA_PORT);
	if (scancode == 0xE0) {
		g_ExtendedKey = true;
		return;
	}

	if (g_ExtendedKey) {
		g_ExtendedKey = false;
		if (scancode == SCANCODE_PAGEUP) {
			Keyboard_PutChar(KEY_PAGEUP);
			return;
		}
		if (scancode == SCANCODE_PAGEDOWN) {
			Keyboard_PutChar(KEY_PAGEDOWN);
			return;
		}
		if (scancode & 0x80)
			return; // extended key release, ignore
		return;
	}

	// Shift press/release
	if (scancode == SCANCODE_LSHIFT || scancode == SCANCODE_RSHIFT) {
		g_ShiftHeld = true;
		return;
	}
	if (scancode == SCANCODE_LSHIFT_REL || scancode == SCANCODE_RSHIFT_REL) {
		g_ShiftHeld = false;
		return;
	}

	// Caps lock toggle
	if (scancode == SCANCODE_CAPS) {
		g_CapsLock = !g_CapsLock;
		return;
	}

	// Ignore key-release events
	if (scancode & 0x80)
		return;

	// Backspace
	if (scancode == SCANCODE_BACKSPACE) {
		Keyboard_PutChar('\b');
		return;
	}

	if (scancode < sizeof(g_ScanToAscii)) {
		char c;
		if (g_ShiftHeld) {
			c = g_ScanToAsciiShift[scancode];
		} else {
			c = g_ScanToAscii[scancode];
			// Apply caps lock to letters only
			if (g_CapsLock && c >= 'a' && c <= 'z')
				c = c - 'a' + 'A';
		}
		if (c) {
			printf("%c", c); // echo to screen
			Keyboard_PutChar(c);
		}
	}
	if (scancode == SCANCODE_PAGEUP) {
		Keyboard_PutChar(KEY_PAGEUP);
		return;
	}
	if (scancode == SCANCODE_PAGEDOWN) {
		Keyboard_PutChar(KEY_PAGEDOWN);
		return;
	}
}

void i686_Keyboard_Initialize() {
	i686_IRQ_RegisterHandler(1, keyboard_handler);
	i686_IRQ_Unmask(1);
}
