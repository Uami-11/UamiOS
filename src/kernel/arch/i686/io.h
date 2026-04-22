#pragma once
#include <stdint.h>

void __attribute__((cdecl)) i686_outb(uint16_t port, uint8_t value);
uint8_t __attribute__((cdecl)) i686_inb(uint16_t port);
uint8_t __attribute__((cdecl)) i686_EnableInterrupts();
uint8_t __attribute__((cdecl)) i686_DisableInterrupts();

void i686_iowait();
void i686_outw(uint16_t port, uint16_t value);
void __attribute__((cdecl)) i686_Panic();

uint16_t i686_inw(uint16_t port);
