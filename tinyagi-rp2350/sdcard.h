#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Mount the SD card via FatFS. Returns true on success.
bool sd_card_init(void);

// Re-assert the SD's SPI baud before an SD access (shared-bus targets where the
// LCD resets the bus clock). No-op-equivalent on dedicated-bus targets.
void sd_reclaim_bus(void);

// A chooser selection: the game name plus where it lives and whether the user
// asked to re-cache it (R). on_sd/in_flash let main() decide caching.
typedef struct {
    char name[32];
    bool on_sd;
    bool in_flash;
    bool refresh;   // user pressed R to force re-cache from SD
} game_choice_t;

// Show a menu merging SD games (/agi/<name>) and flash-cached games, let the
// user pick with UP/DOWN/ENTER (R = force re-cache an SD game). Fills *out.
// Returns false if the user pressed ESC or there are no games.
bool show_dir_chooser(game_choice_t *out);

// Read an entire file from path into a malloc'd buffer.
// Returns NULL on failure. Caller must free with sd_free_file().
uint8_t *sd_read_file(const char *path, size_t *out_size);
void sd_free_file(uint8_t *buf);

// Read len bytes from path starting at byte offset. Returns bytes actually read.
size_t sd_read_file_at(const char *path, size_t offset, void *buf, size_t len);

// Sequential FatFS file I/O for AGI save/restore data.
// sd_save_open: path is the full FatFS path; write=true creates/overwrites.
bool sd_save_open(const char *path, bool write);
bool sd_save_write(const void *buf, size_t len);
bool sd_save_read(void *buf, size_t len);
void sd_save_close(void);
