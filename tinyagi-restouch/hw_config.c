/* FatFS hardware configuration for RESTOUCH SD card.
   SD card shares spi1 with the LCD: MISO=12, MOSI=11, SCK=10, CS=22.
   No card-detect pin. */
#include <assert.h>
#include "hw_config.h"

static spi_t spis[] = {
    {
        .hw_inst   = spi1,
        .miso_gpio = 12,
        .mosi_gpio = 11,
        .sck_gpio  = 10,
        .baud_rate = 12500 * 1000,
        .DMA_IRQ_num = DMA_IRQ_0,
    }
};

static sd_card_t sd_cards[] = {
    {
        .pcName          = "0:",
        .spi             = &spis[0],
        .ss_gpio         = 22,
        .use_card_detect = false,
    }
};

size_t sd_get_num()                  { return count_of(sd_cards); }
sd_card_t *sd_get_by_num(size_t num) { return num < count_of(sd_cards) ? &sd_cards[num] : NULL; }
size_t spi_get_num()                 { return count_of(spis); }
spi_t *spi_get_by_num(size_t num)    { return num < count_of(spis) ? &spis[num] : NULL; }
