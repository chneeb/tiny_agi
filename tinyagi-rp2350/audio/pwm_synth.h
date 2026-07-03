#pragma once

#include <stdint.h>

typedef struct {
    uint16_t hz;
    float sample_pos;
} pwm_synth_audio_channel_state_t;

#define PWM_SYNTH_NUM_CHANNELS 3
extern pwm_synth_audio_channel_state_t pwm_synth_channels[PWM_SYNTH_NUM_CHANNELS];

extern void pwm_synth_init(int pwm_pin_base);
extern void pwm_synth_silence_all_channels();

/* Render `count` signed 16-bit mono samples at `sample_rate` Hz from the current
 * channel state (same 3-channel sine mix as the PWM path).  Used by the DVI
 * HDMI-audio output instead of the PWM sink; advances channel sample_pos. */
extern void pwm_synth_render(int16_t *out, int count, float sample_rate);