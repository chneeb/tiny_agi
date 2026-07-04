#include <stdio.h>
#include <malloc.h>
#include <string.h>
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
#include "flashfs.h"
#include "lcdspi.h"
#include "agi.h"

/* DVI_EMBED_GAME: serve all game files from a flash archive (0x10100000) instead
 * of the SD card — a test to isolate whether SD access causes the transition
 * signal blanking. See dvi/embedded_archive.h + platform.c. */
#ifndef DVI_EMBED_GAME
#define DVI_EMBED_GAME 0
#endif

// FLASHFS_ENABLED: cache games to flash and play from there (fixes the DVI
// transition blanking; SD-optional). 0 = play directly from SD (pre-flashfs
// behavior; SD required). Default on.
#ifndef FLASHFS_ENABLED
#define FLASHFS_ENABLED 1
#endif

extern char game_dir[64];
extern char game_name[32];
extern bool play_from_flash;
extern bool sd_available;
extern bool game_on_sd;

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

/* DVI_DBG_TIMER: periodic UART diagnostic to categorise the "goes dark" failure.
 *   hb   = main-loop heartbeat (core0 game loop alive?)
 *   loop = core1 fill-loop frame count (core1 producing TMDS?)
 *   scan = DVI DMA IRQ frame count   (core1 scanout DMA alive?)
 * Reading it while dark:
 *   hb++ , loop++/scan++  -> everything runs; dark = monitor rejecting signal
 *   hb++ , loop/scan froze -> core1 DVI DMA stopped (signal loss / core1 hung)
 *   hb froze, loop/scan++  -> core0 game loop stuck, core1 fine
 *   no output at all       -> core0 IRQs off (deadlock) or UART dead
 *   "*** HARDFAULT ..."    -> a crash (PC tells where)  */
#ifndef DVI_DBG_TIMER
#define DVI_DBG_TIMER 0
#endif
#if DVI_DBG_TIMER
uint32_t dvi_debug_get_loop_frames(void);
uint32_t dvi_debug_get_scanout_frames(void);
void     dvi_debug_print_fault(void);
static volatile uint32_t g_dbg_hb = 0;
static bool dbg_timer_cb(struct repeating_timer *t) {
    (void)t;
    printf("DVIdbg hb=%lu loop=%lu scan=%lu\n",
           (unsigned long)g_dbg_hb,
           (unsigned long)dvi_debug_get_loop_frames(),
           (unsigned long)dvi_debug_get_scanout_frames());
    dvi_debug_print_fault();
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

    // Mount the flash game/save cache BEFORE core1 (the DVI scanout) starts, so
    // the format-on-first-boot write is single-core (no cross-core coordination).
#if FLASHFS_ENABLED
    flashfs_init();
#endif

    display_init();

    lcd_clear();
#if DVI_EMBED_GAME
    lcd_print_string("Embedded game (flash archive)\n");   /* SD not used */
#else
    lcd_print_string("Mounting SD card...\n");
    sd_available = sd_card_init();
#if FLASHFS_ENABLED
    // SD is optional: without it, only flash-cached games are playable.
    if (!sd_available)
        lcd_print_string("No SD card - flash games only.\n");
    sleep_ms(400);
#else
    // No flash cache: SD is required (games play directly from it).
    if (!sd_available) {
        lcd_print_string("SD mount FAILED!\n");
        while (1) tight_loop_contents();
    }
#endif
#endif

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
#if DVI_DBG_TIMER
    static struct repeating_timer dbg_timer;
    add_repeating_timer_ms(500, dbg_timer_cb, NULL, &dbg_timer);
#endif

    while (1) {
#if !DVI_EMBED_GAME
        game_choice_t choice;
        if (!show_dir_chooser(&choice)) {
            lcd_clear();
            lcd_print_string("No game selected.\n");
            while (1) tight_loop_contents();
        }
        strncpy(game_name, choice.name, sizeof(game_name) - 1);
        game_name[sizeof(game_name) - 1] = '\0';
        game_on_sd = choice.on_sd;
        snprintf(game_dir, sizeof(game_dir), "0:/agi/%s", game_name);

#if FLASHFS_ENABLED
        // Cache from SD on first play (or re-cache on R), then always play from
        // flash — that's what keeps transitions fast (no SD during play). The copy
        // stops core1, so on DVI the screen goes dark and can't show progress;
        // set expectations here, while it's still visible.
        if (choice.refresh || !choice.in_flash) {
            lcd_clear();
            lcd_print_string(choice.refresh ? "Refreshing from SD\n\n"
                                            : "First-time setup\n\n");
            lcd_print_string("Copying ");
            lcd_print_string(game_name);
            lcd_print_string(" to flash.\n\n");
            lcd_print_string("The screen will go DARK for\n");
            lcd_print_string("a bit. Please wait and do\n");
            lcd_print_string("not unplug the device...\n");
            sleep_ms(2500);   // let the message be read before core1 stops
            flashfs_cache_game(game_name, game_dir);
        }
        play_from_flash = true;   // FLASHFS off -> stays false -> get_file reads SD
#endif  /* FLASHFS_ENABLED */
#endif  /* !DVI_EMBED_GAME */

        lcd_clear();
        lcd_print_string("Loading: ");
#if DVI_EMBED_GAME
        lcd_print_string("(embedded)");
#else
        lcd_print_string(game_name);
#endif
        lcd_print_string("\n");
        agi_initialize();

        while (1) {
            uint32_t now_ms = to_ms_since_boot(get_absolute_time());
#if DVI_DBG_TIMER
            g_dbg_hb++;   /* main-loop heartbeat, watched by dbg_timer_cb */
#endif
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
