#pragma once
#include <stdbool.h>

void i686_Keyboard_Initialize();
void Keyboard_PutChar(char c);
char Keyboard_GetChar();
int Keyboard_HasChar();
