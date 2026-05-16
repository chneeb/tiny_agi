#include "display.h"
#include "lcdspi.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <string.h>

// AGI EGA 16-color palette mapped to ILI9488 24-bit RGB.
// RGB() macro from lcdspi.h encodes as (R<<16)|(G<<8)|B.
// draw_rect_spi sends bytes as (c>>16), (c>>8)&0xFF, c&0xFF = R, G, B,
// which is what we replicate in flush_display's line buffer.
static const uint32_t agi_palette[16] = {
    RGB(  0,   0,   0),  //  0  Black
    RGB(  0,   0, 168),  //  1  Dark Blue
    RGB(  0, 168,   0),  //  2  Dark Green
    RGB(  0, 168, 168),  //  3  Dark Cyan
    RGB(168,   0,   0),  //  4  Dark Red
    RGB(168,   0, 168),  //  5  Dark Magenta
    RGB(168,  84,   0),  //  6  Brown
    RGB(168, 168, 168),  //  7  Light Gray
    RGB( 84,  84,  84),  //  8  Dark Gray
    RGB( 84,  84, 255),  //  9  Light Blue
    RGB( 84, 255,  84),  // 10  Light Green
    RGB( 84, 255, 255),  // 11  Light Cyan
    RGB(255,  84,  84),  // 12  Light Red
    RGB(255,  84, 255),  // 13  Light Magenta
    RGB(255, 255,  84),  // 14  Yellow
    RGB(255, 255, 255),  // 15  White
};

// 320×200 framebuffer: one byte per pixel storing AGI color index 0-15.
static uint8_t framebuffer[320 * 200];
// 160×168 priority buffer.
static uint8_t priority_buffer[160 * 168];
// Scratch buffer for one row converted to 24-bit RGB (960 bytes).
static uint8_t line_buf[320 * 3];

// define_region_spi is in lcdspi.c but not declared in lcdspi.h.
// It sets the ILI9488 column+page window and sends the RAMWR command,
// leaving CS low and DC high so pixel data can be streamed immediately.
extern void define_region_spi(int xstart, int ystart, int xend, int yend, int rw);
// draw_rect_spi is also in lcdspi.c but undeclared in the header.
extern void draw_rect_spi(int x1, int y1, int x2, int y2, int c);

// Vertical offset so the 320×200 AGI frame is centred in the 320×320 TFT.
#define DISPLAY_Y_OFFSET 60

// Build one 960-byte RGB row from the indexed framebuffer row and stream it.
static inline void send_row(const uint8_t *row) {
    for (int x = 0; x < 320; x++) {
        uint32_t c = agi_palette[row[x] & 0x0F];
        line_buf[x * 3]     = (uint8_t)(c >> 16);
        line_buf[x * 3 + 1] = (uint8_t)(c >>  8);
        line_buf[x * 3 + 2] = (uint8_t) c;
    }
    hw_send_spi(line_buf, 320 * 3);
}

void display_init(void) {
    lcd_init();
    // Bump SPI1 from 25 MHz (init-safe) to 50 MHz for faster frame pushes.
    // ILI9488 supports up to ~66 MHz write cycles.
    spi_set_baudrate(spi1, 50000000);

    memset(framebuffer,     0, sizeof(framebuffer));
    memset(priority_buffer, 0, sizeof(priority_buffer));

    // Paint the 60-row top and bottom margins black so they are never stale.
    draw_rect_spi(0,   0, 319,  59, BLACK);
    draw_rect_spi(0, 260, 319, 319, BLACK);
}

void screen_set_160(int x, int y, int color) {
    if ((unsigned)x >= 160 || (unsigned)y >= 200) return;
    framebuffer[y * 320 + x * 2]     = (uint8_t)color;
    framebuffer[y * 320 + x * 2 + 1] = (uint8_t)color;
}

void screen_set_320(int x, int y, int color) {
    if ((unsigned)x >= 320 || (unsigned)y >= 200) return;
    framebuffer[y * 320 + x] = (uint8_t)color;
}

int priority_get(int x, int y) {
    if ((unsigned)x >= 160 || (unsigned)y >= 168) return 0;
    return priority_buffer[y * 160 + x];
}

void priority_set(int x, int y, int priority) {
    if ((unsigned)x >= 160 || (unsigned)y >= 168) return;
    priority_buffer[y * 160 + x] = (uint8_t)priority;
}

void agi_shake_screen(uint8_t times) {
    for (uint8_t i = 0; i < times * 2; i++) {
        // Alternate ±2 rows to produce the shake effect.
        int yoff = DISPLAY_Y_OFFSET + ((i & 1) ? 2 : -2);
        define_region_spi(0, yoff, 319, yoff + 199, 1);
        for (int y = 0; y < 200; y++)
            send_row(&framebuffer[y * 320]);
        spi_finish(spi1);
        lcd_spi_raise_cs();
        sleep_ms(30);
    }
    // Restore correct position.
    flush_display();
}

void flush_display(void) {
    define_region_spi(0, DISPLAY_Y_OFFSET, 319, DISPLAY_Y_OFFSET + 199, 1);
    for (int y = 0; y < 200; y++)
        send_row(&framebuffer[y * 320]);
    spi_finish(spi1);
    lcd_spi_raise_cs();
}
