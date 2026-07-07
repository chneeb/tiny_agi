/* FatFS hardware configuration for the DVI-target SD card.
   RP2350-PiZero: spi1, SCLK=30, MOSI=31, MISO=40, CS=43 (dedicated bus, no LCD sharing).
   RP2040-PiZero: spi0, SCK=18,  MOSI=19, MISO=20, CS=21 (onboard microSD). */
#include <assert.h>
#include "hw_config.h"

static spi_t spis[] = {
    {
#if RP2040_PIZERO
        .hw_inst     = spi0,
        .miso_gpio   = 20,
        .mosi_gpio   = 19,
        .sck_gpio    = 18,
#else
        .hw_inst     = spi1,
        .miso_gpio   = 40,
        .mosi_gpio   = 31,
        .sck_gpio    = 30,
#endif
        .baud_rate   = 40000 * 1000,   /* overclocked from 12.5 MHz (card-dependent); speeds caching */
        .DMA_IRQ_num = DMA_IRQ_1,  /* DVI owns DMA_IRQ_0 on core1; SD uses DMA_IRQ_1 on core0 */
    }
};

static sd_card_t sd_cards[] = {
    {
        .pcName          = "0:",
        .spi             = &spis[0],
#if RP2040_PIZERO
        .ss_gpio         = 21,
#else
        .ss_gpio         = 43,
#endif
        .use_card_detect = false,
    }
};

size_t sd_get_num()                  { return count_of(sd_cards); }
sd_card_t *sd_get_by_num(size_t num) { return num < count_of(sd_cards) ? &sd_cards[num] : NULL; }
size_t spi_get_num()                 { return count_of(spis); }
spi_t *spi_get_by_num(size_t num)    { return num < count_of(spis) ? &spis[num] : NULL; }
