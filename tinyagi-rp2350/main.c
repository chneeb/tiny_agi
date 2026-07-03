#include <stdio.h>
#include <malloc.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#if USE_VREG_BOOST
#include "hardware/vreg.h"
#endif
#if SOUND_ENABLED
#include "audio/pwm_synth.h"
#include "agi_sound_player/agi_sound.h"
#endif

#include "display.h"
#include "kbd_input.h"
#include "sdcard.h"
#include "lcdspi.h"
#include "agi.h"

extern char game_dir[64];

#if DVI_TARGET
#include "pico/time.h"

/* A/B experiment for the intermittent room-transition hang.
 * DVI_KEEPALIVE_TIMER=1: register a pure no-op periodic timer.  Its only effect
 * is that core0 takes a timer IRQ every DVI_KEEPALIVE_MS ms — a regular wakeup
 * source.  If this alone makes the hang go away, the bug is a lost-wakeup /
 * IRQ-timing race (e.g. the SD `__wfe` wait under DVI-DMA contention).
 * DVI_KEEPALIVE_TIMER=0: no timer at all — the baseline that crashed.
 * Nothing else differs between the two builds. */
#ifndef DVI_KEEPALIVE_TIMER
#define DVI_KEEPALIVE_TIMER 1
#endif
#ifndef DVI_KEEPALIVE_MS
#define DVI_KEEPALIVE_MS 250
#endif

#if DVI_KEEPALIVE_TIMER
static bool keepalive_cb(struct repeating_timer *t) {
    (void)t;              /* pure no-op: the wakeup IS the effect */
    return true;
}
#endif

#if DVI_HDMI_AUDIO
/* HDMI-audio producer (see dvi/display.cpp).  A ~2 ms timer keeps pico_lib's
 * ring topped up from the current 3-channel synth state, independent of the
 * main loop — so audio doesn't drop out during blocking room loads.  It
 * self-clocks: it only writes what core1 has drained (getWritableSize). */
uint32_t dvi_audio_writable(void);
void     dvi_audio_write(const int16_t *mono, int n);
void     dvi_audio_init(void);

static bool audio_producer_cb(struct repeating_timer *t) {
    (void)t;
    int16_t buf[128];
    uint32_t w = dvi_audio_writable();
    int n = w > 128 ? 128 : (int)w;
    if (n > 0) {
        pwm_synth_render(buf, n, 44100.0f);
        dvi_audio_write(buf, n);
    }
    return true;
}
#endif

/* Large core0 stack in main SRAM.  The default RP2350 layout puts core0's
 * stack in SCRATCH_Y (only 4 KB), directly above the DVI TMDS encode loops in
 * SCRATCH_X.  Deep AGI logic recursion (call/new_room) + FatFs + TinyUSB
 * overflow 4 KB and grow down into SCRATCH_X, corrupting the encoder that
 * core1 is executing → core1 faults and the screen goes dark.  We move core0's
 * stack here so it can never reach SCRATCH_X.  32 KB is ample headroom. */
static uint8_t core0_stack[32 * 1024] __attribute__((aligned(16)));

static void agi_main(void);

int main(void) {
    /* Switch MSP to the big stack, then tail-call the real main.  Must be the
     * first thing here and must not touch pre-switch locals afterwards. */
    uint32_t sp = (uint32_t)core0_stack + sizeof(core0_stack);
    __asm volatile("msr msp, %0 \n mov sp, %0 \n" : : "r"(sp) : "memory");
    agi_main();
    while (1) tight_loop_contents();
}

static void agi_main(void) {
#else
int main(void) {
#endif
#if USE_VREG_BOOST
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
#endif
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    stdio_init_all();

    kbd_input_init();
#if DVI_TARGET
    cdc_stdio_init(); /* USB-C → serial terminal for printf debug output */
#endif
    display_init();

    lcd_clear();
    lcd_print_string("Mounting SD card...\n");
    if (!sd_card_init()) {
        lcd_print_string("SD mount FAILED!\n");
        while (1) tight_loop_contents();
    }

#if SOUND_ENABLED
#if DVI_HDMI_AUDIO
    dvi_audio_init();   /* after display_init: dvi_inst exists */
    static struct repeating_timer audio_timer;
    add_repeating_timer_ms(2, audio_producer_cb, NULL, &audio_timer);
#else
    pwm_synth_init(AUDIO_PIN);
#endif
#endif

#if DVI_TARGET && DVI_KEEPALIVE_TIMER
    static struct repeating_timer keepalive_timer;
    add_repeating_timer_ms(DVI_KEEPALIVE_MS, keepalive_cb, NULL, &keepalive_timer);
#endif

    while (1) {
        if (!show_dir_chooser(game_dir, sizeof(game_dir))) {
            lcd_clear();
            lcd_print_string("No game selected.\n");
            while (1) tight_loop_contents();
        }

        lcd_clear();
        lcd_print_string("Loading: ");
        lcd_print_string(game_dir);
        lcd_print_string("\n");
        agi_initialize();

        while (1) {
            uint32_t now_ms = to_ms_since_boot(get_absolute_time());

            check_key();

            bool did_run = agi_logic_run_cycle(now_ms);
            if (did_run) {
                flush_display();
#if SOUND_ENABLED
                platform_tick_sound();
#endif
            }

            if (state.game_state == STATE_QUIT) break;
        }

        agi_stop_sound();
        lcd_clear();
    }
}
