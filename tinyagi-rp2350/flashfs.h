#pragma once
// Flash-resident littlefs: caches AGI games (and saves) in the RP2350's spare
// flash so games load at flash speed instead of from the slow SD card. On the
// DVI target this is what stops the room-transition signal blanking; on the LCD
// targets it's a faster-loads / SD-optional enhancement. Shared by all targets.
//
// Flash layout (offset from 0x10000000):
//   0x000000 .. 0x100000   firmware (1 MB reserved)
//   0x100000 .. flash_end  littlefs region (size = PICO_FLASH_SIZE_BYTES - 1 MB)
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Mount the flash filesystem (formats on first-ever use). Returns true on success.
bool flashfs_init(void);

// Self-test: write a temp file, read it back, verify, delete. true = OK.
// Proves the block device + flash region are correct before we rely on them.
bool flashfs_selftest(void);

// Bytes available in the littlefs region (for diagnostics / "flash full").
uint32_t flashfs_region_size(void);

// Cache usage for the boot menu: total region, used, and free bytes (any may be
// NULL). Uses lfs_fs_size(); safe when unmounted (reports 0 used). Note used can
// slightly overcount (littlefs counts shared metadata per-reference) — clamped.
void flashfs_df(uint32_t *total, uint32_t *used, uint32_t *free_bytes);

// ── Game cache ───────────────────────────────────────────────────────────────
// Is <name> cached in flash (/games/<name> exists)?
bool flashfs_has_game(const char *name);
// Copy an SD game dir (src_sd_dir e.g. "0:/agi/SQ1") into /games/<name>, streaming
// each file. Overwrites any existing cache (used for first-cache and refresh).
// Wraps the whole copy in the flash-write lock (pauses core1 on DVI). Returns true.
bool flashfs_cache_game(const char *name, const char *src_sd_dir);
// List cached game names into names[max]; returns the count.
int  flashfs_list_games(char names[][32], int max);
// Delete a cached game (/games/<name> and its files). Returns true.
bool flashfs_delete_game(const char *name);

// ── Game reads (served to get_file / read_file_at for the playing game) ──────
// Whole file from /games/<game>/<filename> into a malloc'd buffer (caller frees).
uint8_t *flashfs_read_file(const char *game, const char *filename, size_t *out_size);
// Range read from /games/<game>/<filename>; returns bytes read.
size_t   flashfs_read_at(const char *game, const char *filename,
                         size_t offset, void *buf, size_t len);

// ── Save games in flash (/saves/<name>.sav) ─────────────────────────────────
// Streaming, mirrors the sd_save_* API. A write-mode open holds the flash-write
// lock until close (pauses core1 on DVI for the duration of the save).
bool   flashfs_save_open(const char *name, bool write);
bool   flashfs_save_write(const void *buf, size_t len);
size_t flashfs_save_read(void *buf, size_t len);
void   flashfs_save_close(void);

// Flash-write critical section. Flash erase/program disables XIP, so no core may
// execute from flash during a write. Weak no-op default (fine for single-core
// LCD targets); the DVI target overrides these to pause/resume core1's scanout.
void flashfs_write_lock(void);
void flashfs_write_unlock(void);
