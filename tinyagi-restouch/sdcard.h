#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

bool sd_card_init(void);

// Game chooser: scans /agi/ on the SD card and writes chosen path into out_path.
bool show_dir_chooser(char *out_path, size_t len);

uint8_t *sd_read_file(const char *path, size_t *out_size);
void     sd_free_file(uint8_t *buf);
size_t   sd_read_file_at(const char *path, size_t offset, void *buf, size_t len);

bool sd_save_open(const char *path, bool write);
bool sd_save_write(const void *buf, size_t len);
bool sd_save_read(void *buf, size_t len);
void sd_save_close(void);
