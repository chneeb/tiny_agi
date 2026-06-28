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

int main(void) {
#if USE_VREG_BOOST
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
#endif
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    stdio_init_all();

    kbd_input_init();
    display_init();

    lcd_clear();
    lcd_print_string("Mounting SD card...\n");
    if (!sd_card_init()) {
        lcd_print_string("SD mount FAILED!\n");
        while (1) tight_loop_contents();
    }

#if SOUND_ENABLED
    pwm_synth_init(26);
#endif

    while (1) {
        if (!show_dir_chooser(game_dir, sizeof(game_dir))) {
            lcd_clear();
            lcd_print_string("No game selected.\n");
            while (1) tight_loop_contents();
        }

        printf("Starting: %s\n", game_dir);
        lcd_clear();
        agi_initialize();

        uint32_t cycle_count = 0;

        while (1) {
            uint32_t now_ms = to_ms_since_boot(get_absolute_time());

            check_key();

            bool did_run = agi_logic_run_cycle(now_ms);
            if (did_run) {
                flush_display();
#if SOUND_ENABLED
                platform_tick_sound();
#endif
                if (++cycle_count % 200 == 0) {
                    struct mallinfo mi = mallinfo();
                    printf("heap free: %d  used: %d  cycles: %lu\n",
                           mi.fordblks, mi.uordblks, (unsigned long)cycle_count);
                }
            }

            if (state.game_state == STATE_QUIT) break;
        }

        agi_stop_sound();
        lcd_clear();
    }
}
