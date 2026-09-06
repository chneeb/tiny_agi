/* tinyagi-sdl — desktop SDL2 port.
 *
 * Display, keyboard and audio output for the platform-independent engine in
 * agi/. The sound sequencer (agi_sound_player/) and the synth mixer core
 * (audio/pwm_synth_core.c) are shared verbatim with the RP2350 targets; only
 * the output stage differs (SDL audio device instead of PWM/I2S/HDMI).
 *
 * Usage: tinyagi_sdl <game-dir> [--scale N] [--save FILE]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "agi.h"
/* Shared with the RP2350 targets; included by path — see the note in
 * CMakeLists.txt about audio/strings.h shadowing the C library header. */
#include "../tinyagi-rp2350/agi_sound_player/agi_sound.h"
#include "../tinyagi-rp2350/audio/pwm_synth.h"
#include "sdl_platform.h"
#include "../agi/src/actions.h"   /* new_room() for the --room debug flag */

#define AGI_W 320
#define AGI_H 200
#define PRI_W 160
#define PRI_H 168

// AGI EGA 16-colour palette, 0xAARRGGBB (matches the RP2350 targets).
static const uint32_t agi_palette[16] = {
    0xFF000000, 0xFF0000A8, 0xFF00A800, 0xFF00A8A8,
    0xFFA80000, 0xFFA800A8, 0xFFA85400, 0xFFA8A8A8,
    0xFF545454, 0xFF5454FF, 0xFF54FF54, 0xFF54FFFF,
    0xFFFF5454, 0xFFFF54FF, 0xFFFFFF54, 0xFFFFFFFF,
};

// One byte per pixel storing the AGI colour index (same model as the Pico ports).
static uint8_t framebuffer[AGI_W * AGI_H];
static uint8_t priority_buffer[PRI_W * PRI_H];
// Converted ARGB frame handed to the streaming texture.
static uint32_t pixels[AGI_W * AGI_H];

static SDL_Window   *window;
static SDL_Renderer *renderer;
static SDL_Texture  *texture;

static int  shake_offset_x = 0;
static int  shake_offset_y = 0;
static bool show_priority = false;   // TAB: overlay the priority buffer

// Headless/debug helpers: run a fixed number of cycles and/or dump the frame.
static int  exit_after_frames = 0;   // 0 = run until quit
static const char *shot_path = NULL;
static int  jump_room = -1;          // --room N: jump there once the game is up
static const char *pic_dump_path = NULL;  // --dump-pic: raw pic visual+priority
static int  ego_x = -1, ego_y = -1;  // --ego X,Y: park ego there (debug)
static int  ego_pri = -1;            // --ego-pri N: fixed priority, set BEFORE --room
static int  ego_pri_late = -1;       // --ego-pri-late N: fixed priority, set AFTER --room
static int  pic_only = -1;           // --pic N: draw pic N, dump it, exit
static const char *script_keys = NULL;  // --keys: feed these keystrokes, one per 20 frames
static int  script_pos = 0;

// -----------------------------------------------------------------------
// Framebuffer access (platform_support.h)
// -----------------------------------------------------------------------
void screen_set_160(int x, int y, int color) {
    if ((unsigned)x >= PRI_W || (unsigned)y >= AGI_H) return;
    framebuffer[y * AGI_W + x * 2]     = (uint8_t)color;
    framebuffer[y * AGI_W + x * 2 + 1] = (uint8_t)color;
}

void screen_set_320(int x, int y, int color) {
    if ((unsigned)x >= AGI_W || (unsigned)y >= AGI_H) return;
    framebuffer[y * AGI_W + x] = (uint8_t)color;
}

void priority_set(int x, int y, int priority) {
    if ((unsigned)x >= PRI_W || (unsigned)y >= PRI_H) return;
    priority_buffer[y * PRI_W + x] = (uint8_t)priority;
}

int priority_get(int x, int y) {
    if ((unsigned)x >= PRI_W || (unsigned)y >= PRI_H) return 0;
    return priority_buffer[y * PRI_W + x];
}

// -----------------------------------------------------------------------
// Presentation
// -----------------------------------------------------------------------
static void render(void) {
    if (show_priority) {
        for (int y = 0; y < PRI_H; y++)
            for (int x = 0; x < PRI_W; x++)
                screen_set_160(x, y, priority_get(x, y));
    }

    for (int i = 0; i < AGI_W * AGI_H; i++)
        pixels[i] = agi_palette[framebuffer[i] & 0x0F];

    SDL_UpdateTexture(texture, NULL, pixels, AGI_W * (int)sizeof(uint32_t));
    SDL_RenderClear(renderer);

    if (shake_offset_x || shake_offset_y) {
        // SDL_RenderSetLogicalSize makes the destination rect logical-space,
        // so the shake offset is in AGI pixels regardless of window scale.
        SDL_Rect dst = { shake_offset_x, shake_offset_y, AGI_W, AGI_H };
        SDL_RenderCopy(renderer, texture, NULL, &dst);
    } else {
        SDL_RenderCopy(renderer, texture, NULL, NULL);
    }
    SDL_RenderPresent(renderer);
}

void flush_display(void) {
    render();
}

void agi_shake_screen(uint8_t times) {
    for (int i = 0; i < 5 * times; i++) {
        shake_offset_x = (rand() % 20) - 10;
        shake_offset_y = (rand() % 20) - 10;
        render();
        SDL_Delay(30);
    }
    shake_offset_x = shake_offset_y = 0;
    render();
}

// Write the current frame to a BMP (debug aid: headless smoke tests and
// differential comparison against the Pico render).
static void save_screenshot(const char *path) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels, AGI_W, AGI_H, 32, AGI_W * (int)sizeof(uint32_t),
        SDL_PIXELFORMAT_ARGB8888);
    if (!s) return;
    if (SDL_SaveBMP(s, path) != 0)
        fprintf(stderr, "Screenshot failed: %s\n", SDL_GetError());
    SDL_FreeSurface(s);
}

// -----------------------------------------------------------------------
// Keyboard.  SDL2 splits typed text (SDL_TEXTINPUT) from key events
// (SDL_KEYDOWN); ascii comes from the former, scancodes from the latter.
// -----------------------------------------------------------------------
static void handle_keydown(const SDL_KeyboardEvent *k) {
    uint8_t scancode = 0;
    char ascii = 0;

    switch (k->keysym.sym) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:   state.enter_pressed = true; return;
        case SDLK_UP:         scancode = AGI_KEY_UP;    break;
        case SDLK_DOWN:       scancode = AGI_KEY_DOWN;  break;
        case SDLK_LEFT:       scancode = AGI_KEY_LEFT;  break;
        case SDLK_RIGHT:      scancode = AGI_KEY_RIGHT; break;
        case SDLK_KP_8:       scancode = AGI_KEY_UP;    break;
        case SDLK_KP_2:       scancode = AGI_KEY_DOWN;  break;
        case SDLK_KP_4:       scancode = AGI_KEY_LEFT;  break;
        case SDLK_KP_6:       scancode = AGI_KEY_RIGHT; break;
        case SDLK_KP_7:
        case SDLK_HOME:       scancode = AGI_KEY_HOME;  break;
        case SDLK_KP_9:
        case SDLK_PAGEUP:     scancode = AGI_KEY_PGUP;  break;
        case SDLK_KP_1:
        case SDLK_END:        scancode = AGI_KEY_END;   break;
        case SDLK_KP_3:
        case SDLK_PAGEDOWN:   scancode = AGI_KEY_PGDN;  break;
        case SDLK_INSERT:     scancode = AGI_KEY_INS;   break;
        case SDLK_DELETE:     scancode = AGI_KEY_DEL;   break;
        case SDLK_BACKSPACE:  ascii = '\b';             break;
        case SDLK_TAB:        show_priority = !show_priority; return;
        case SDLK_ESCAPE:     ascii = 27;               break;
        case SDLK_F1:  case SDLK_F2:  case SDLK_F3:  case SDLK_F4:
        case SDLK_F5:  case SDLK_F6:  case SDLK_F7:  case SDLK_F8:
        case SDLK_F9:
            scancode = (uint8_t)(AGI_KEY_F1 + (k->keysym.sym - SDLK_F1));
            break;
        case SDLK_F10: scancode = AGI_KEY_F10; break;
        default: return;
    }

    if (ascii || scancode)
        agi_input_queue_push_keypress(ascii, scancode);
}

// Pump the event queue. Returns false if the window was closed.
static bool pump_events(void) {
    SDL_Event e;
    bool running = true;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
                handle_keydown(&e.key);
                break;
            case SDL_TEXTINPUT: {
                // Printable ASCII only; the engine's parser is 7-bit.
                char c = e.text.text[0];
                if (c >= 0x20 && c < 0x7F)
                    agi_input_queue_push_keypress(c, 0);
                break;
            }
        }
    }
    return running;
}

// -----------------------------------------------------------------------
// Audio output.  The SDL callback runs on its own thread and only advances
// pwm_synth_channels[].sample_pos; the game thread only writes .hz — the same
// split the PWM ISR has on the Pico.
// -----------------------------------------------------------------------
#define AUDIO_RATE 44100

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    pwm_synth_render((int16_t *)stream, len / (int)sizeof(int16_t), (float)AUDIO_RATE);
}

static void audio_init(void) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = AUDIO_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 1024;
    want.callback = audio_callback;

    if (SDL_OpenAudio(&want, &have) < 0) {
        fprintf(stderr, "Audio unavailable (%s) - continuing silently.\n", SDL_GetError());
        return;
    }
    SDL_PauseAudio(0);
}

// -----------------------------------------------------------------------
// Engine callbacks that block (platform_support.h)
// -----------------------------------------------------------------------
void platform_tick_sound(void) {
    /* Runtime Sound on/off (menu / F2): FLAG_9 mutes the audio *output* only —
       the sequencer keeps running so sound-paced logic stays timed. */
    pwm_synth_set_muted(!state.flags[FLAG_9_SOUND_ENABLED]);

    static uint32_t last_ms = 0;
    static uint32_t accum_ms = 0;
    static bool first_call = true;

    uint32_t now_ms = SDL_GetTicks();
    if (first_call) {
        last_ms = now_ms;
        first_call = false;
        return;
    }
    uint32_t delta = now_ms - last_ms;
    last_ms = now_ms;

    // Cap delta to avoid fast-forwarding sound after a long blocking call.
    if (delta > 150) delta = 150;
    accum_ms += delta;

    // 1 AGI sound tick = 1/60 s ~ 16.67 ms
    int ticks = (int)(accum_ms * 60 / 1000);
    if (ticks <= 0) return;
    accum_ms -= (uint32_t)(ticks * 1000 / 60);

    if (state.sound_flag > -1 && agi_sound_is_playing()) {
        if (!agi_sound_tick(ticks)) {
            state.flags[state.sound_flag] = true;
            state.sound_flag = -1;
            agi_stop_sound();
        }
    }
}

void platform_flush_display(void) {
    flush_display();
    platform_tick_sound();
}

// Feed the --keys script. Driven from check_key() rather than the main loop so
// it also answers the engine's have_key() polling loops (e.g. "Press any key to
// continue"), which never return to the main loop.
static void script_tick(void) {
    static int ticks = 0;
    if (!script_keys || !script_keys[script_pos]) return;
    if (++ticks < 20) return;
    ticks = 0;
    char c = script_keys[script_pos++];
    switch (c) {
        case '\n': state.enter_pressed = true; break;
        case '.':  break;                                    /* idle tick */
        case '^':  agi_input_queue_push_keypress(0, AGI_KEY_UP);    break;
        case 'v':  agi_input_queue_push_keypress(0, AGI_KEY_DOWN);  break;
        case '<':  agi_input_queue_push_keypress(0, AGI_KEY_LEFT);  break;
        case '>':  agi_input_queue_push_keypress(0, AGI_KEY_RIGHT); break;
        default:   agi_input_queue_push_keypress(c, 0);      break;
    }
}

void check_key(void) {
    if (!pump_events())
        state.game_state = STATE_QUIT;
    script_tick();
}

void wait_for_enter(void) {
    flush_display();
    state.enter_pressed = false;
    // Scripted runs (--keys) can't answer a modal: the main loop that feeds the
    // script isn't running while we block here. Auto-dismiss after a moment.
    int polls = 0;
    while (!state.enter_pressed) {
        if (script_keys && ++polls > 20)
            return;
        if (!pump_events()) {
            state.game_state = STATE_QUIT;
            return;
        }
        platform_tick_sound();
        SDL_Delay(10);
    }
    // Consumed here — the flag must not leak into the next cycle (see
    // "enter_pressed lifecycle" in CLAUDE.md).
    state.enter_pressed = false;
}

bool wait_for_key_yn(void) {
    flush_display();
    while (1) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                state.game_state = STATE_QUIT;
                return false;
            }
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_y) return true;
                if (k == SDLK_n || k == SDLK_ESCAPE) return false;
            }
        }
        SDL_Delay(10);
    }
}

// -----------------------------------------------------------------------
int main(int argc, char **argv) {
    int scale = 3;
    const char *dir = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = atoi(argv[++i]);
            if (scale < 1) scale = 1;
        } else if (strcmp(argv[i], "--pic") == 0 && i + 1 < argc) {
            pic_only = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--keys") == 0 && i + 1 < argc) {
            script_keys = argv[++i];
        } else if (strcmp(argv[i], "--ego-pri-late") == 0 && i + 1 < argc) {
            ego_pri_late = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ego-pri") == 0 && i + 1 < argc) {
            ego_pri = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ego") == 0 && i + 1 < argc) {
            sscanf(argv[++i], "%d,%d", &ego_x, &ego_y);
        } else if (strcmp(argv[i], "--dump-pic") == 0 && i + 1 < argc) {
            pic_dump_path = argv[++i];
        } else if (strcmp(argv[i], "--pri") == 0) {
            show_priority = true;
        } else if (strcmp(argv[i], "--room") == 0 && i + 1 < argc) {
            jump_room = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            exit_after_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shot_path = argv[++i];
        } else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) {
            snprintf(save_path, sizeof(save_path), "%s", argv[++i]);
        } else if (argv[i][0] != '-') {
            dir = argv[i];
        } else {
            fprintf(stderr, "Usage: %s <game-dir> [--scale N] [--save FILE] [--frames N] [--shot FILE.bmp]\n", argv[0]);
            return 1;
        }
    }
    if (!dir) {
        fprintf(stderr, "Usage: %s <game-dir> [--scale N] [--save FILE] [--frames N] [--shot FILE.bmp]\n", argv[0]);
        return 1;
    }
    snprintf(game_dir, sizeof(game_dir), "%s", dir);
    platform_scan_game_dir();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   // nearest-neighbour
    window = SDL_CreateWindow("TinyAGI",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              AGI_W * scale, AGI_H * scale,
                              SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 1;
    }
    // Letterboxes and scales the 320x200 frame for us, at any window size.
    SDL_RenderSetLogicalSize(renderer, AGI_W, AGI_H);

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, AGI_W, AGI_H);
    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_StartTextInput();
    audio_init();

    agi_initialize();

    int frames = 0;
    while (1) {
        uint32_t now_ms = SDL_GetTicks();

        check_key();

        if (agi_logic_run_cycle(now_ms)) {
            flush_display();
            platform_tick_sound();
        }

        if (state.game_state == STATE_QUIT) break;
        if (exit_after_frames && ++frames >= exit_after_frames) break;
        // Debug: drop straight into a room once the first room is running.
        // Debug: draw one picture into the pic buffers and stop, so the picture
        // renderer can be diffed against a reference implementation. Done from
        // inside the loop so the engine is fully initialised first.
        if (pic_only >= 0 && frames == 60) {
            load_pic_no((uint8_t)pic_only);
            draw_pic_no((uint8_t)pic_only);
            break;
        }
        if (jump_room >= 0 && frames == 60) {
            new_room((uint8_t)jump_room);
            jump_room = -1;
        }
        // Applied *before* the --room jump so the jump's new_room() gets a
        // chance to clear it (that is the fix under test).
        if (ego_pri >= 0 && frames == 30) {
            state.objects[0].has_fixed_priority = true;
            state.objects[0].fixed_priority = (uint8_t)ego_pri;
        }
        if (ego_pri_late >= 0 && frames == 100) {
            state.objects[0].has_fixed_priority = true;
            state.objects[0].fixed_priority = (uint8_t)ego_pri_late;
        }
        if (ego_x >= 0 && frames >= 60) {
            state.objects[0].x = (uint8_t)ego_x;
            state.objects[0].y = (uint8_t)ego_y;
        }

        SDL_Delay(1);
    }

    // Debug: the picture's own visual/priority screens (before sprites/text),
    // for diffing against a reference AGI picture renderer.
    if (pic_dump_path) {
        FILE *f = fopen(pic_dump_path, "wb");
        if (f) {
            for (int y = 0; y < 168; y++)
                for (int x = 0; x < 160; x++) {
                    fputc(pic_vis_get(x, y), f);
                    fputc(pic_pri_get(x, y), f);
                }
            fclose(f);
        }
    }

    if (shot_path) {
        render();
        save_screenshot(shot_path);
    }

    agi_stop_sound();
    SDL_CloseAudio();
    SDL_Quit();
    return 0;
}
