#pragma once
#include <stdint.h>
#include <stddef.h>

// ST7789 320x240 SPI driver.
// Pins are provided as compile-time defines from CMakeLists.txt:
//   LCD_PIN_DC, LCD_PIN_CS, LCD_PIN_CLK, LCD_PIN_MOSI, LCD_PIN_RST, LCD_PIN_BL
//   LCD_SPI_PORT  (e.g. spi1)
//   SD_PIN_CS, TOUCH_PIN_CS  (driven HIGH before LCD init on shared SPI bus)

// font_data is defined in platform.c and used by lcd_print_string().
extern uint8_t font_data[2048];

// Initialise SPI bus, reset display, run DCS init sequence, enable backlight.
// Must be called before any other lcd_ function.
// On SHARED_SPI_BUS: drives SD_PIN_CS and TOUCH_PIN_CS HIGH before touching the bus.
void lcdspi_init(void);

// Set the GRAM write window (CASET + RASET + RAMWR).
// After this call DC is high and CS is low; call lcdspi_write_data() to push pixels,
// then lcdspi_end_write() to deassert CS.
void lcdspi_set_address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

// Write raw bytes to the display (DC stays high, CS stays low).
// Must be preceded by lcdspi_set_address() or lcdspi_begin_data().
void lcdspi_write_data(const uint8_t *data, size_t len);

// Raise CS after a streaming pixel write.
void lcdspi_end_write(void);

// Fill the entire 320x240 screen with black.
void lcd_clear(void);

// Print a null-terminated string using the 8x8 CP437 font.
// Newlines advance the cursor down one character row.
// Used only for startup / error messages before the game starts.
void lcd_print_string(const char *s);
