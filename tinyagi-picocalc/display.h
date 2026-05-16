#pragma once
#include <stdint.h>

// Initialize TFT display (calls lcd_init, bumps SPI to 50 MHz, clears margins)
void display_init(void);

// Push entire 320×200 framebuffer to the TFT.
// Call once after agi_draw_all_active().
void flush_display(void);

// AGI platform display interface (platform_support.h)
void screen_set_160(int x, int y, int color);
void screen_set_320(int x, int y, int color);
int  priority_get(int x, int y);
void priority_set(int x, int y, int priority);
void agi_shake_screen(uint8_t times);
