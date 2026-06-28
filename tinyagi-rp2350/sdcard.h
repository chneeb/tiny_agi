#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Mount the SD card via FatFS. Returns true on success.
bool sd_card_init(void);

// Show a menu of /agi/<subdir>/ directories and let the user pick one
// using UP/DOWN/ENTER. Stores the chosen game directory in out_path
// (e.g. "0:/agi/KQ1"). Returns false if user pressed ESC.
bool show_dir_chooser(char *out_path, size_t len);

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
