#include "kbd_input.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

// M5Stack CardKB I2C address
#define CARDKB_ADDR 0x5F

void kbd_input_init(void)
{
    i2c_init(KB_I2C_PORT, 100000);
    gpio_set_function(KB_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(KB_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(KB_PIN_SDA);
    gpio_pull_up(KB_PIN_SCL);
}

int kbd_read(void)
{
    uint8_t key = 0;
    int r = i2c_read_blocking(KB_I2C_PORT, CARDKB_ADDR, &key, 1, false);
    if (r < 0 || key == 0)
        return -1;
    return (int)key;
}
