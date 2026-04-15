#include "keyboard.h"
#include "io.h"
#include "irq.h"
#include <stdio.h>

#define KEYBOARD_DATA_PORT 0x60

// US keyboard scancode to ASCII (set 1, key-down only)
static const char g_ScanToAscii[] = {
	0,	 0,	  '1', '2', '3', '4', '5', '6', '7', '8', '9',	'0', '-', '=',	0,
	0,	 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',	'[', ']', '\n', 0,
	'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,	  '\\', 'z',
	'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,	  '*',	0,	 ' '};

static void keyboard_handler(Registers *regs) {
	uint8_t scancode = i686_inb(KEYBOARD_DATA_PORT);

	// ignore key-release events (bit 7 set)
	if (scancode & 0x80)
		return;

	if (scancode < sizeof(g_ScanToAscii) && g_ScanToAscii[scancode])
		printf("%c", g_ScanToAscii[scancode]);
}

void i686_Keyboard_Initialize() {
	i686_IRQ_RegisterHandler(1, keyboard_handler);
	i686_IRQ_Unmask(1);
}
