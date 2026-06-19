#pragma once
#include <stdint.h>

void display_init(void);
void flush_display(void);

void screen_set_160(int x, int y, int color);
void screen_set_320(int x, int y, int color);
int  priority_get(int x, int y);
void priority_set(int x, int y, int priority);
void agi_shake_screen(uint8_t times);
