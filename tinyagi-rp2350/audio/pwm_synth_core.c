/* Portable synth core: channel state, mute flag and the software mixer.
 *
 * Split out of pwm_synth.c so the mixer can be shared by every output backend
 * without dragging in the Pico SDK: the PWM ISR (pwm_synth.c), the I2S DAC
 * (i2s_output.c), the DVI HDMI data-island path (dvi/display.cpp) and the SDL
 * desktop port (tinyagi-sdl) all consume it. This file must stay free of
 * hardware headers.
 */
#include "pwm_synth.h"
#include "strings.h"

#include <stddef.h>

// Sine wave sample rate = 8272Hz, sample tone ~= 523Hz (C4)
const int sample_length = 6924;

pwm_synth_audio_channel_state_t pwm_synth_channels[PWM_SYNTH_NUM_CHANNELS];

/* Output mute (driven by FLAG_9_SOUND_ENABLED via pwm_synth_set_muted). The
 * sequencer keeps running (timing preserved for sound-paced logic); only the
 * audio output is silenced. Checked by every output path. */
volatile bool pwm_synth_muted = false;
void pwm_synth_set_muted(bool m) { pwm_synth_muted = m; }

void pwm_synth_silence_all_channels() {
    for (size_t i = 0; i < PWM_SYNTH_NUM_CHANNELS; i++)
    {
        pwm_synth_channels[i].hz = 0;
    }
}

// Render mono int16 samples at an arbitrary sample rate (HDMI/I2S/SDL audio).
// Mirrors pih()'s mix, but centred/scaled to signed 16-bit and rate-parameterised
// so it stays in tune at 44100 Hz while the PWM path keeps its native 22050 Hz.
void pwm_synth_render(int16_t *out, int count, float sample_rate) {
    if (pwm_synth_muted) {                      /* sound off: emit silence */
        for (int i = 0; i < count; i++) out[i] = 0;
        return;
    }
    const float ncps = sample_rate / 25.0f;   // matches num_sine_curves_per_sec
    for (int j = 0; j < count; j++) {
        uint16_t combined = 0;
        for (size_t i = 0; i < PWM_SYNTH_NUM_CHANNELS; i++) {
            pwm_synth_audio_channel_state_t *state = &pwm_synth_channels[i];
            if (state->hz == 0) {
                combined += 127;
            } else {
                uint16_t sample = strings[(int)state->sample_pos] + 127;
                state->sample_pos += state->hz / ncps;
                if (state->sample_pos >= sample_length)
                    state->sample_pos -= sample_length;
                combined += sample;
            }
        }
        uint16_t mixed = combined / PWM_SYNTH_NUM_CHANNELS;   // 0..254, midpoint 127
        out[j] = (int16_t)(((int)mixed - 127) << 7);          // ~±16 k signed
    }
}
