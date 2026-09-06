/* tinyagi-sdl — platform layer: file I/O, save data, sound glue, panic.
 *
 * Mirrors tinyagi-rp2350/platform.c; the SD/flash routing collapses to plain
 * stdio here. Display/input/audio-output live in main.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

#include "agi.h"
/* Shared with the RP2350 targets; included by path — see the note in
 * CMakeLists.txt about audio/strings.h shadowing the C library header. */
#include "../tinyagi-rp2350/agi_sound_player/agi_sound.h"
#include "../tinyagi-rp2350/audio/pwm_synth.h"
#include "sdl_platform.h"

char game_dir[512] = ".";
char save_path[512] = "savegame.sav";

void panic(const char *fmt, ...) {
    va_list vl;
    va_start(vl, fmt);
    fprintf(stderr, "Panic! ");
    vfprintf(stderr, fmt, vl);
    fprintf(stderr, "\n");
    va_end(vl);
    exit(1);
}

// -----------------------------------------------------------------------
// Filename resolution.
// The engine asks for bare lowercase names ("logdir", "object", "vol.0").
// FatFs on the Pico is case-insensitive; a Linux filesystem is not, and real
// AGI game directories are usually uppercase (LOGDIR, VOL.0). Scan the game
// directory once and map lowercase -> the actual on-disk name.
// -----------------------------------------------------------------------
#define MAX_DIR_ENTRIES 1024
static char dir_names[MAX_DIR_ENTRIES][64];
static int  dir_count = 0;

void platform_scan_game_dir(void) {
    dir_count = 0;
    DIR *d = opendir(game_dir);
    if (!d) panic("Cannot open game directory '%s'", game_dir);
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (strlen(e->d_name) >= sizeof(dir_names[0])) continue;
        if (dir_count == MAX_DIR_ENTRIES) {
            // GOG installs put hundreds of unrelated files next to the game;
            // warn rather than silently failing to resolve a resource later.
            fprintf(stderr, "Warning: more than %d files in '%s' - "
                            "some may not be found.\n", MAX_DIR_ENTRIES, game_dir);
            break;
        }
        strcpy(dir_names[dir_count++], e->d_name);
    }
    closedir(d);
}

// Build the full path for an engine-requested filename, case-insensitively.
static void resolve_path(const char *filename, char *out, size_t out_size) {
    for (int i = 0; i < dir_count; i++) {
        if (strcasecmp(dir_names[i], filename) == 0) {
            snprintf(out, out_size, "%s/%s", game_dir, dir_names[i]);
            return;
        }
    }
    // Not found in the scan — fall through with the literal name so the
    // caller's fopen() reports the miss.
    snprintf(out, out_size, "%s/%s", game_dir, filename);
}

// -----------------------------------------------------------------------
// File access
// -----------------------------------------------------------------------
agi_file_t get_file(const char *filename) {
    char path[1024];
    resolve_path(filename, path, sizeof(path));

    agi_file_t result = { NULL, 0 };
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "FILE FAIL: %s\n", path);
        return result;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0) {
        result.data = (uint8_t *)malloc((size_t)size);
        if (!result.data)
            panic("Out of memory reading %s (%ld bytes)", path, size);
        result.size = fread(result.data, 1, (size_t)size, f);
    }
    fclose(f);
    return result;
}

void free_file(agi_file_t file) {
    free(file.data);
}

size_t read_file_at(const char *filename, size_t offset, uint8_t *buf, size_t len) {
    char path[1024];
    resolve_path(filename, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "FILE FAIL: %s\n", path);
        return 0;
    }
    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    size_t n = fread(buf, 1, len, f);
    fclose(f);
    return n;
}

// -----------------------------------------------------------------------
// Save / restore
// -----------------------------------------------------------------------
agi_save_data_file_ptr agi_save_data_open(const char *mode) {
    return (agi_save_data_file_ptr)fopen(save_path, mode);
}

void agi_save_data_write(agi_save_data_file_ptr file_ptr, void *data, size_t size) {
    if (file_ptr) fwrite(data, size, 1, (FILE *)file_ptr);
}

void agi_save_data_read(agi_save_data_file_ptr file_ptr, void *destination, size_t size) {
    if (file_ptr) {
        if (fread(destination, size, 1, (FILE *)file_ptr) != 1)
            memset(destination, 0, size);
    }
}

void agi_save_data_close(agi_save_data_file_ptr file_ptr) {
    if (file_ptr) fclose((FILE *)file_ptr);
}

// -----------------------------------------------------------------------
// Sound — the sequencer is shared with the Pico targets; see
// "Sound timing decoupled from audio output" in CLAUDE.md.
// -----------------------------------------------------------------------
void agi_play_sound(uint8_t *sound_data) {
    agi_sound_start(sound_data);
}

void agi_stop_sound(void) {
    agi_sound_stop();
}

void platform_debug_flush(void) {
    fflush(stdout);
}
