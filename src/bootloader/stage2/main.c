#include "stdint.h"
#include "stdio.h"

void _cdecl cstart_(uint16_t bootDrive) {
	puts("\r\n");

	puts(" _   _                 _ _____ _____ \r\n");
	puts("| | | |               (_)  _  /  ___|\r\n");
	puts("| | | | __ _ _ __ ___  _| | | \\ `--. \r\n");
	puts("| | | |/ _` | '_ ` _ \\| | | | |`--. \\\r\n");
	puts("| |_| | (_| | | | | | | \\ \\_/ /\\__/ /\r\n");
	puts(" \\___/ \\__,_|_| |_| |_|_|\\___/\\____/ \r\n");

	puts("\r\nWelcome to UamiOS!\r\n");

	for (;;)
		;
}
