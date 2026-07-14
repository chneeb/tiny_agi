#include "i2s_output.h"
#include "pwm_synth.h"
#include "audio_i2s.pio.h"        // generated from audio_i2s.pio

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include <stdint.h>

#ifndef I2S_DATA_PIN
#define I2S_DATA_PIN 26            // DIN
#endif
#ifndef I2S_CLOCK_PIN_BASE
#define I2S_CLOCK_PIN_BASE 27      // BCK=27, LRCK=28 (must be consecutive)
#endif
#ifndef I2S_SAMPLE_RATE
#define I2S_SAMPLE_RATE 22050
#endif

// Per-buffer sample count. The DMA read-address ring wraps every I2S_NSAMPLES*4
// bytes = 2^I2S_RING_BITS, so the two must stay in sync (256 words = 1024 B = 2^10).
#define I2S_NSAMPLES  256
#define I2S_RING_BITS 10

static PIO  i2s_pio = pio0;        // free on RESTOUCH (LCD is hardware SPI)
static uint i2s_sm;
static int  dma_a, dma_b;
static volatile int last_active = -1;
static struct repeating_timer i2s_timer;

// Two ping-pong buffers, aligned so the DMA read ring wraps within each.
static uint32_t buf_a[I2S_NSAMPLES] __attribute__((aligned(I2S_NSAMPLES * 4)));
static uint32_t buf_b[I2S_NSAMPLES] __attribute__((aligned(I2S_NSAMPLES * 4)));

// Mix the 3 AGI channels to mono (pwm_synth_render — returns silence when
// FLAG_9 muted), duplicate into the L/R halves of each 32-bit I2S word.
static void i2s_fill(uint32_t *dst) {
    int16_t mono[I2S_NSAMPLES];
    pwm_synth_render(mono, I2S_NSAMPLES, (float)I2S_SAMPLE_RATE);
    for (int i = 0; i < I2S_NSAMPLES; i++) {
        uint16_t s = (uint16_t)mono[i];
        dst[i] = ((uint32_t)s << 16) | s;
    }
}

// The two DMA channels chain to each other (gapless ping-pong). This timer tops
// up whichever buffer just stopped playing — once per swap. Buffers are
// I2S_NSAMPLES/rate long (~11.6 ms at 22050); the 4 ms tick catches every swap.
static bool i2s_producer_cb(struct repeating_timer *t) {
    (void)t;
    if (dma_channel_is_busy(dma_a)) {
        if (last_active != dma_a) { last_active = dma_a; i2s_fill(buf_b); }
    } else if (dma_channel_is_busy(dma_b)) {
        if (last_active != dma_b) { last_active = dma_b; i2s_fill(buf_a); }
    }
    return true;
}

static void i2s_config_channel(int chan, int chain_to, uint32_t *buf) {
    dma_channel_config c = dma_channel_get_default_config(chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(i2s_pio, i2s_sm, true));
    channel_config_set_chain_to(&c, chain_to);
    channel_config_set_ring(&c, false, I2S_RING_BITS);   // wrap read addr within the buffer
    dma_channel_configure(chan, &c, &i2s_pio->txf[i2s_sm], buf, I2S_NSAMPLES, false);
}

void i2s_output_init(void) {
    for (int i = 0; i < I2S_NSAMPLES; i++) { buf_a[i] = 0; buf_b[i] = 0; }

    i2s_sm = (uint)pio_claim_unused_sm(i2s_pio, true);
    pio_gpio_init(i2s_pio, I2S_DATA_PIN);
    pio_gpio_init(i2s_pio, I2S_CLOCK_PIN_BASE);
    pio_gpio_init(i2s_pio, I2S_CLOCK_PIN_BASE + 1);

    uint offset = pio_add_program(i2s_pio, &audio_i2s_program);
    audio_i2s_program_init(i2s_pio, i2s_sm, offset, I2S_DATA_PIN, I2S_CLOCK_PIN_BASE);

    // Bit-clock divider (msxemulator's proven formula: sysclk*4/rate as 24.8 fixed).
    uint32_t divider = clock_get_hz(clk_sys) * 4 / I2S_SAMPLE_RATE;
    pio_sm_set_clkdiv_int_frac(i2s_pio, i2s_sm, divider >> 8u, divider & 0xffu);

    dma_a = dma_claim_unused_channel(true);
    dma_b = dma_claim_unused_channel(true);
    i2s_config_channel(dma_a, dma_b, buf_a);
    i2s_config_channel(dma_b, dma_a, buf_b);

    dma_channel_start(dma_a);
    add_repeating_timer_ms(4, i2s_producer_cb, NULL, &i2s_timer);
}
