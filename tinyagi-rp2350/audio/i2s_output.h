#pragma once

// I2S audio output (PIO + DMA ping-pong double buffer), fed by the AGI sound
// sequencer via pwm_synth_render(). For targets with an external I2S DAC —
// e.g. RESTOUCH + a Waveshare Pico Audio shield on GP26/27/28
// (DIN=26, BCK=27, LRCK=28). Enable with I2S_AUDIO + SOUND_ENABLED.
//
// Independent of the display (its own PIO SM + DMA channels), so it does not
// disturb the LCD/DVI signal. Respects the Sound on/off toggle for free:
// pwm_synth_render() already returns silence when FLAG_9 muted.
void i2s_output_init(void);
