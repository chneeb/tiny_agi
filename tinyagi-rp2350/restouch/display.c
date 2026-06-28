#include "display.h"
#include "lcdspi.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <string.h>

// AGI EGA 16-colour palette in RGB565.
// RGB565(r,g,b) = ((r>>3)<<11) | ((g>>2)<<5) | (b>>3)
#define RGB565(r,g,b) (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3))

static const uint16_t agi_palette[16] = {
    RGB565(  0,   0,   0),  //  0  Black
    RGB565(  0,   0, 168),  //  1  Dark Blue
    RGB565(  0, 168,   0),  //  2  Dark Green
    RGB565(  0, 168, 168),  //  3  Dark Cyan
    RGB565(168,   0,   0),  //  4  Dark Red
    RGB565(168,   0, 168),  //  5  Dark Magenta
    RGB565(168,  84,   0),  //  6  Brown
    RGB565(168, 168, 168),  //  7  Light Gray
    RGB565( 84,  84,  84),  //  8  Dark Gray
    RGB565( 84,  84, 255),  //  9  Light Blue
    RGB565( 84, 255,  84),  // 10  Light Green
    RGB565( 84, 255, 255),  // 11  Light Cyan
    RGB565(255,  84,  84),  // 12  Light Red
    RGB565(255,  84, 255),  // 13  Light Magenta
    RGB565(255, 255,  84),  // 14  Yellow
    RGB565(255, 255, 255),  // 15  White
};

// 320×200 framebuffer: one byte per pixel, AGI colour index 0-15.
static uint8_t framebuffer[320 * 200];
// 160×168 priority buffer.
static uint8_t priority_buffer[160 * 168];
// Scratch buffer: one row in RGB565 (2 bytes per pixel).
static uint8_t line_buf[320 * 2];

// AGI 320×200 centred in ST7789 320×240 → 20-row black margins top and bottom.
#define DISPLAY_Y_OFFSET 20

static inline void send_row(const uint8_t *row)
{
    for (int x = 0; x < 320; x++) {
        uint16_t c = agi_palette[row[x] & 0x0F];
        line_buf[x * 2]     = (uint8_t)(c >> 8);
        line_buf[x * 2 + 1] = (uint8_t)(c & 0xFF);
    }
    lcdspi_write_data(line_buf, 320 * 2);
}

void display_init(void)
{
    lcdspi_init();

    memset(framebuffer,     0, sizeof(framebuffer));
    memset(priority_buffer, 0, sizeof(priority_buffer));

    // Paint top and bottom 20-row margins black.
    static const uint8_t black2[2] = {0x00, 0x00};
    lcdspi_set_address(0, 0, 319, DISPLAY_Y_OFFSET - 1);
    for (int i = 0; i < 320 * DISPLAY_Y_OFFSET; i++)
        lcdspi_write_data(black2, 2);
    lcdspi_end_write();

    lcdspi_set_address(0, DISPLAY_Y_OFFSET + 200, 319, 239);
    for (int i = 0; i < 320 * DISPLAY_Y_OFFSET; i++)
        lcdspi_write_data(black2, 2);
    lcdspi_end_write();
}

void screen_set_160(int x, int y, int color)
{
    if ((unsigned)x >= 160 || (unsigned)y >= 200) return;
    framebuffer[y * 320 + x * 2]     = (uint8_t)color;
    framebuffer[y * 320 + x * 2 + 1] = (uint8_t)color;
}

void screen_set_320(int x, int y, int color)
{
    if ((unsigned)x >= 320 || (unsigned)y >= 200) return;
    framebuffer[y * 320 + x] = (uint8_t)color;
}

int priority_get(int x, int y)
{
    if ((unsigned)x >= 160 || (unsigned)y >= 168) return 0;
    return priority_buffer[y * 160 + x];
}

void priority_set(int x, int y, int priority)
{
    if ((unsigned)x >= 160 || (unsigned)y >= 168) return;
    priority_buffer[y * 160 + x] = (uint8_t)priority;
}

void agi_shake_screen(uint8_t times)
{
    for (uint8_t i = 0; i < times * 2; i++) {
        int yoff = DISPLAY_Y_OFFSET + ((i & 1) ? 2 : -2);
        lcdspi_set_address(0, yoff, 319, yoff + 199);
        for (int y = 0; y < 200; y++)
            send_row(&framebuffer[y * 320]);
        lcdspi_end_write();
        sleep_ms(30);
    }
    flush_display();
}

void flush_display(void)
{
    lcdspi_set_address(0, DISPLAY_Y_OFFSET, 319, DISPLAY_Y_OFFSET + 199);
    for (int y = 0; y < 200; y++)
        send_row(&framebuffer[y * 320]);
    lcdspi_end_write();
}
