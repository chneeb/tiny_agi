#include "lcdspi.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include <string.h>

// ── DCS command codes ────────────────────────────────────────────────────────
#define DCS_SOFT_RESET           0x01
#define DCS_EXIT_SLEEP_MODE      0x11
#define DCS_ENTER_INVERT_MODE    0x21
#define DCS_SET_DISPLAY_ON       0x29
#define DCS_SET_COLUMN_ADDRESS   0x2A
#define DCS_SET_PAGE_ADDRESS     0x2B
#define DCS_WRITE_MEMORY_START   0x2C
#define DCS_SET_ADDRESS_MODE     0x36
#define DCS_SET_PIXEL_FORMAT     0x3A

// RGB | SWAP_XY | MIRROR_Y → landscape 320x240 with correct orientation
#define DISPLAY_ADDRESS_MODE  0xA0
// 16-bit colour
#define DISPLAY_PIXEL_FORMAT  0x55

// ── Low-level SPI helpers ────────────────────────────────────────────────────

static inline void write_command(uint8_t cmd)
{
    gpio_put(LCD_PIN_DC, 0);
    gpio_put(LCD_PIN_CS, 0);
    spi_write_blocking(LCD_SPI_PORT, &cmd, 1);
    gpio_put(LCD_PIN_CS, 1);
}

static inline void write_data(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_CS, 0);
    spi_write_blocking(LCD_SPI_PORT, data, len);
    gpio_put(LCD_PIN_CS, 1);
}

// ── Public API ───────────────────────────────────────────────────────────────

void lcdspi_set_address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint8_t data[4];

    write_command(DCS_SET_COLUMN_ADDRESS);
    data[0] = x1 >> 8; data[1] = x1 & 0xFF;
    data[2] = x2 >> 8; data[3] = x2 & 0xFF;
    write_data(data, 4);

    write_command(DCS_SET_PAGE_ADDRESS);
    data[0] = y1 >> 8; data[1] = y1 & 0xFF;
    data[2] = y2 >> 8; data[3] = y2 & 0xFF;
    write_data(data, 4);

    write_command(DCS_WRITE_MEMORY_START);
    // Leave DC high, CS low — caller streams pixel data then calls lcdspi_end_write()
    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_CS, 0);
}

void lcdspi_write_data(const uint8_t *data, size_t len)
{
    spi_write_blocking(LCD_SPI_PORT, data, len);
}

void lcdspi_end_write(void)
{
    gpio_put(LCD_PIN_CS, 1);
}

void lcd_clear(void)
{
    static const uint8_t black[2] = {0x00, 0x00};
    lcdspi_set_address(0, 0, 319, 239);
    for (int i = 0; i < 320 * 240; i++)
        spi_write_blocking(LCD_SPI_PORT, black, 2);
    lcdspi_end_write();
}

// ── Startup text rendering ───────────────────────────────────────────────────

static int text_col = 0;
static int text_row = 0;

static void draw_char_direct(int col, int row, unsigned char c, uint16_t fg, uint16_t bg)
{
    uint8_t pixels[8 * 8 * 2];
    const unsigned char *glyph = &font_data[c * 8];
    for (int y = 0; y < 8; y++) {
        uint8_t bits = glyph[y];
        for (int x = 0; x < 8; x++) {
            uint16_t colour = (bits & (0x80 >> x)) ? fg : bg;
            pixels[(y * 8 + x) * 2]     = colour >> 8;
            pixels[(y * 8 + x) * 2 + 1] = colour & 0xFF;
        }
    }
    lcdspi_set_address(col * 8, row * 8, col * 8 + 7, row * 8 + 7);
    lcdspi_write_data(pixels, sizeof(pixels));
    lcdspi_end_write();
}

void lcd_print_string(const char *s)
{
    // White on black: 0xFFFF fg, 0x0000 bg
    while (*s) {
        if (*s == '\n') {
            text_col = 0;
            text_row++;
        } else {
            if (text_col < 40 && text_row < 30)
                draw_char_direct(text_col, text_row, (unsigned char)*s, 0xFFFF, 0x0000);
            text_col++;
            if (text_col >= 40) { text_col = 0; text_row++; }
        }
        s++;
    }
}

// ── Init ─────────────────────────────────────────────────────────────────────

void lcdspi_init(void)
{
    // On SHARED_SPI_BUS, park SD and touch CS high before touching the bus
    // so they don't fight MISO during LCD init traffic.
#ifdef SD_PIN_CS
    gpio_init(SD_PIN_CS);
    gpio_set_dir(SD_PIN_CS, GPIO_OUT);
    gpio_put(SD_PIN_CS, 1);
#endif
#ifdef TOUCH_PIN_CS
    gpio_init(TOUCH_PIN_CS);
    gpio_set_dir(TOUCH_PIN_CS, GPIO_OUT);
    gpio_put(TOUCH_PIN_CS, 1);
#endif

    // Route clk_peri from clk_sys so SPI sees the full system clock.
    uint32_t freq = clock_get_hz(clk_sys);
    clock_configure(clk_peri, 0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, freq, freq);

    // GPIO setup
    gpio_set_function(LCD_PIN_DC,   GPIO_FUNC_SIO); gpio_set_dir(LCD_PIN_DC,   GPIO_OUT);
    gpio_set_function(LCD_PIN_CS,   GPIO_FUNC_SIO); gpio_set_dir(LCD_PIN_CS,   GPIO_OUT);
    gpio_put(LCD_PIN_CS, 1);

    gpio_set_function(LCD_PIN_CLK,  GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SPI);

    spi_init(LCD_SPI_PORT, LCD_SPI_CLOCK_HZ);
    spi_set_baudrate(LCD_SPI_PORT, LCD_SPI_CLOCK_HZ);

    // Hardware reset
    gpio_set_function(LCD_PIN_RST, GPIO_FUNC_SIO); gpio_set_dir(LCD_PIN_RST, GPIO_OUT);
    gpio_put(LCD_PIN_RST, 0); sleep_ms(100);
    gpio_put(LCD_PIN_RST, 1); sleep_ms(100);

    // DCS init sequence
    uint8_t param;
    write_command(DCS_SOFT_RESET);     sleep_ms(200);

    write_command(DCS_SET_ADDRESS_MODE);
    param = DISPLAY_ADDRESS_MODE;      write_data(&param, 1);

    write_command(DCS_SET_PIXEL_FORMAT);
    param = DISPLAY_PIXEL_FORMAT;      write_data(&param, 1);

    write_command(DCS_ENTER_INVERT_MODE); // ST7789 needs inversion for correct colours

    write_command(DCS_EXIT_SLEEP_MODE);  sleep_ms(200);
    write_command(DCS_SET_DISPLAY_ON);   sleep_ms(200);

    // Enable backlight
    gpio_set_function(LCD_PIN_BL, GPIO_FUNC_SIO); gpio_set_dir(LCD_PIN_BL, GPIO_OUT);
    gpio_put(LCD_PIN_BL, 1);
}
