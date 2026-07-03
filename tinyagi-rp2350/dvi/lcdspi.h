#pragma once
#include <stdint.h>
#include <stddef.h>

/* Stub for the DVI target: lcd_clear() and lcd_print_string() are
   implemented in display.cpp, not in a real SPI LCD driver. */

extern uint8_t font_data[2048];  /* defined in platform.c */

void lcd_clear(void);
void lcd_print_string(const char *s);
