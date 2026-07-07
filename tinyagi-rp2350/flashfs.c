#include "flashfs.h"
#include "lfs.h"

#include "pico/stdlib.h"
#include "hardware/flash.h"   // flash_range_erase/program, FLASH_SECTOR_SIZE, FLASH_PAGE_SIZE
#include "hardware/sync.h"    // save_and_disable_interrupts
#include "hardware/regs/addressmap.h"  // XIP_BASE
#include "ff.h"                        // FatFs (SD source for caching)
#include "sdcard.h"                    // sd_reclaim_bus (shared-bus SD baud)

#include <string.h>
#include <strings.h>   // strcasecmp
#include <stdio.h>
#include <stdlib.h>

// ── Flash region geometry ────────────────────────────────────────────────────
// FLASHFS_OFFSET is the byte offset from 0x10000000 where littlefs starts (space
// below it is reserved for firmware). Overridable per target — e.g. push it
// higher to skip a bad lower flash region on a specific board. The region runs to
// the end of flash: FLASHFS_SIZE = PICO_FLASH_SIZE_BYTES - FLASHFS_OFFSET.
#ifndef FLASHFS_OFFSET
#define FLASHFS_OFFSET  (1024u * 1024u)                          // default: 1 MB firmware reserve
#endif
#define FLASHFS_SIZE    ((uint32_t)(PICO_FLASH_SIZE_BYTES) - FLASHFS_OFFSET)
#define FLASHFS_BLOCK   (FLASH_SECTOR_SIZE)                       // 4096 (erase unit)

// ── Flash-write critical section (weak; DVI target provides real ones) ───────
__attribute__((weak)) void flashfs_write_lock(void)   {}
__attribute__((weak)) void flashfs_write_unlock(void) {}

// ── littlefs block device: reads via XIP, writes via the SDK flash API ───────
static int bd_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                   void *buffer, lfs_size_t size) {
    memcpy(buffer,
           (const void *)(XIP_BASE + FLASHFS_OFFSET + (uint32_t)block * c->block_size + off),
           size);
    return 0;
}

static int bd_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                   const void *buffer, lfs_size_t size) {
    uint32_t addr = FLASHFS_OFFSET + (uint32_t)block * c->block_size + off;
    uint32_t ints = save_and_disable_interrupts();   // no core0 IRQ (may run from flash)
    flash_range_program(addr, (const uint8_t *)buffer, size);
    restore_interrupts(ints);
    return 0;
}

static int bd_erase(const struct lfs_config *c, lfs_block_t block) {
    uint32_t addr = FLASHFS_OFFSET + (uint32_t)block * c->block_size;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(addr, c->block_size);
    restore_interrupts(ints);
    return 0;
}

static int bd_sync(const struct lfs_config *c) { (void)c; return 0; }

// ── State ────────────────────────────────────────────────────────────────────
static lfs_t             lfs;
static struct lfs_config cfg;
static bool              mounted = false;

static uint8_t rbuf[256];   // read cache
static uint8_t pbuf[256];   // prog cache
static uint8_t lbuf[256];   // lookahead

static void fill_cfg(void) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.read  = bd_read;
    cfg.prog  = bd_prog;
    cfg.erase = bd_erase;
    cfg.sync  = bd_sync;
    cfg.read_size      = 256;
    cfg.prog_size      = FLASH_PAGE_SIZE;             // 256
    cfg.block_size     = FLASHFS_BLOCK;               // 4096
    cfg.block_count    = FLASHFS_SIZE / FLASHFS_BLOCK;
    cfg.cache_size     = 256;
    cfg.lookahead_size = 256;
    cfg.block_cycles   = 500;                         // wear-leveling
    cfg.read_buffer      = rbuf;
    cfg.prog_buffer      = pbuf;
    cfg.lookahead_buffer = lbuf;
}

uint32_t flashfs_region_size(void) { return FLASHFS_SIZE; }

void flashfs_df(uint32_t *total, uint32_t *used, uint32_t *free_bytes) {
    uint32_t t = FLASHFS_SIZE, u = 0;
    if (mounted) {
        lfs_ssize_t blocks = lfs_fs_size(&lfs);
        if (blocks >= 0) {
            u = (uint32_t)blocks * FLASHFS_BLOCK;
            if (u > t) u = t;   // lfs_fs_size can overcount shared metadata
        }
    }
    if (total)      *total      = t;
    if (used)       *used       = u;
    if (free_bytes) *free_bytes = t - u;
}

bool flashfs_init(void) {
    fill_cfg();
    flashfs_write_lock();
    int err = lfs_mount(&lfs, &cfg);
    if (err == 0 && lfs_fs_size(&lfs) < 0) {
        // Mounts (superblock OK) but a traversal reports corruption — a partial
        // fs left by an earlier interrupted/failed write. Reformat to recover.
        printf("flashfs: mounted but corrupt; reformatting\n");
        lfs_unmount(&lfs);
        err = -1;
    }
    if (err) {
        // First-ever boot (or corrupt): format then mount.
        printf("flashfs: mount failed (%d), formatting %u KB...\n", err, (unsigned)(FLASHFS_SIZE / 1024));
        lfs_format(&lfs, &cfg);
        err = lfs_mount(&lfs, &cfg);
    }
    flashfs_write_unlock();
    mounted = (err == 0);
    printf("flashfs: %s (%u KB region, %u blocks)\n",
           mounted ? "mounted" : "MOUNT FAILED",
           (unsigned)(FLASHFS_SIZE / 1024), (unsigned)cfg.block_count);
    return mounted;
}

bool flashfs_selftest(void) {
    if (!mounted) return false;

    static const char  path[] = "selftest.bin";
    static const char  msg[]  = "flashfs-ok-0123456789-abcdef";
    uint8_t            fcache[256];
    struct lfs_file_config fc = {0};
    fc.buffer = fcache;
    lfs_file_t f;

    // Write (needs the flash-write lock).
    flashfs_write_lock();
    int err = lfs_file_opencfg(&lfs, &f, path,
                               LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &fc);
    if (err >= 0) {
        lfs_file_write(&lfs, &f, msg, sizeof(msg));
        err = lfs_file_close(&lfs, &f);
    }
    flashfs_write_unlock();
    if (err < 0) { printf("flashfs selftest: write err %d\n", err); return false; }

    // Read back (reads are plain XIP — no lock).
    uint8_t buf[sizeof(msg)] = {0};
    err = lfs_file_opencfg(&lfs, &f, path, LFS_O_RDONLY, &fc);
    if (err < 0) { printf("flashfs selftest: open-read err %d\n", err); return false; }
    lfs_ssize_t n = lfs_file_read(&lfs, &f, buf, sizeof(buf));
    lfs_file_close(&lfs, &f);

    bool ok = (n == (lfs_ssize_t)sizeof(msg)) && (memcmp(buf, msg, sizeof(msg)) == 0);
    printf("flashfs selftest: %s (read %d bytes)\n", ok ? "PASS" : "FAIL", (int)n);

    // Tidy up (best-effort).
    flashfs_write_lock();
    lfs_remove(&lfs, path);
    flashfs_write_unlock();
    return ok;
}

// ── Game cache ───────────────────────────────────────────────────────────────
static uint8_t g_fcache[256];    // littlefs per-file cache (one lfs file open at a time)
static uint8_t g_copybuf[2048];  // SD -> flash streaming chunk

bool flashfs_has_game(const char *name) {
    if (!mounted) return false;
    char gdir[64];
    snprintf(gdir, sizeof(gdir), "games/%s", name);
    struct lfs_info info;
    return lfs_stat(&lfs, gdir, &info) >= 0 && info.type == LFS_TYPE_DIR;
}

bool flashfs_delete_game(const char *name) {
    if (!mounted) return false;
    char gdir[64];
    snprintf(gdir, sizeof(gdir), "games/%s", name);

    // Collect file names first (don't mutate the dir while iterating it).
    char files[24][40];
    int nf = 0;
    lfs_dir_t dir;
    struct lfs_info info;
    if (lfs_dir_open(&lfs, &dir, gdir) >= 0) {
        while (nf < 24 && lfs_dir_read(&lfs, &dir, &info) > 0) {
            if (info.type == LFS_TYPE_REG) {
                strncpy(files[nf], info.name, sizeof(files[0]) - 1);
                files[nf][sizeof(files[0]) - 1] = '\0';
                nf++;
            }
        }
        lfs_dir_close(&lfs, &dir);
    }

    flashfs_write_lock();
    for (int i = 0; i < nf; i++) {
        char fp[96];
        snprintf(fp, sizeof(fp), "%s/%s", gdir, files[i]);
        lfs_remove(&lfs, fp);
    }
    lfs_remove(&lfs, gdir);   // now-empty dir
    flashfs_write_unlock();
    printf("flashfs: deleted cache '%s'\n", name);
    return true;
}

int flashfs_list_games(char names[][32], int max) {
    if (!mounted) return 0;
    lfs_dir_t dir;
    if (lfs_dir_open(&lfs, &dir, "games") < 0) return 0;
    struct lfs_info info;
    int n = 0;
    while (n < max && lfs_dir_read(&lfs, &dir, &info) > 0) {
        if (info.type == LFS_TYPE_DIR && info.name[0] != '.') {
            strncpy(names[n], info.name, 31);
            names[n][31] = '\0';
            n++;
        }
    }
    lfs_dir_close(&lfs, &dir);
    return n;
}

// Only the AGI resource files are worth caching — skip the DOS interpreter,
// overlays, docs, save files, etc. that may sit in the game folder.
static bool is_agi_resource(const char *name) {
    if (strncasecmp(name, "vol.", 4) == 0) return true;
    static const char *keep[] = {
        "logdir", "picdir", "viewdir", "snddir", "object", "words.tok"
    };
    for (size_t i = 0; i < sizeof(keep) / sizeof(keep[0]); i++)
        if (strcasecmp(name, keep[i]) == 0) return true;
    return false;
}

// Copy one SD file to littlefs, streaming (files can be >200 KB). Assumes the
// flash-write lock is already held.
static bool copy_one_file(const char *sd_path, const char *lfs_path) {
    FIL fil;
    FRESULT fr = f_open(&fil, sd_path, FA_READ);
    if (fr != FR_OK) { printf("  cache: f_open('%s') FR=%d\n", sd_path, fr); return false; }
    struct lfs_file_config fc = {0};
    fc.buffer = g_fcache;
    lfs_file_t lf;
    int le = lfs_file_opencfg(&lfs, &lf, lfs_path,
                              LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &fc);
    if (le < 0) { printf("  cache: lfs_open('%s') err=%d\n", lfs_path, le); f_close(&fil); return false; }
    bool ok = true;
    size_t total = 0;
    for (;;) {
        UINT br = 0;
        fr = f_read(&fil, g_copybuf, sizeof(g_copybuf), &br);
        if (fr != FR_OK) { printf("  cache: f_read FR=%d at %u\n", fr, (unsigned)total); ok = false; break; }
        if (br == 0) break;
        lfs_ssize_t w = lfs_file_write(&lfs, &lf, g_copybuf, br);
        if (w != (lfs_ssize_t)br) {
            printf("  cache: lfs_write err=%d (%u B) at offset %u\n", (int)w, (unsigned)br, (unsigned)total);
            ok = false;
            break;
        }
        total += br;
    }
    int ce = lfs_file_close(&lfs, &lf);
    if (ok && ce < 0) { printf("  cache: lfs_close err=%d\n", ce); ok = false; }
    f_close(&fil);
    return ok;
}

bool flashfs_cache_game(const char *name, const char *src_sd_dir) {
    if (!mounted) return false;

    sd_reclaim_bus();       // shared-bus: SD reads at the SD baud (LCD left it high)
    flashfs_write_lock();   // pause core1 (DVI) for the whole copy

    lfs_mkdir(&lfs, "games");            // ignore result (may already exist)
    char gdir[64];
    snprintf(gdir, sizeof(gdir), "games/%s", name);
    lfs_mkdir(&lfs, gdir);               // ignore EEXIST

    DIR dir;
    FILINFO fno;
    bool ok = (f_opendir(&dir, src_sd_dir) == FR_OK);
    if (!ok) printf("flashfs: f_opendir('%s') failed\n", src_sd_dir);
    while (ok) {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == '\0') break;
        if (fno.fattrib & AM_DIR) continue;
        if (!is_agi_resource(fno.fname)) continue;   // game data only (skip DOS exe, overlays, saves)

        char sd_path[160], lfs_path[96];
        snprintf(sd_path, sizeof(sd_path), "%s/%s", src_sd_dir, fno.fname);
        snprintf(lfs_path, sizeof(lfs_path), "%s/%s", gdir, fno.fname);
        // Engine requests bare lowercase names ("logdir", "vol.0"), so store them
        // lowercase (SD holds them uppercase).
        for (char *p = lfs_path + strlen(gdir) + 1; *p; p++)
            if (*p >= 'A' && *p <= 'Z') *p += 32;

        if (!copy_one_file(sd_path, lfs_path)) { ok = false; break; }
    }
    f_closedir(&dir);

    flashfs_write_unlock();
    printf("flashfs: cache '%s' %s\n", name, ok ? "OK" : "FAILED");
    return ok;
}

// ── Game reads (served to get_file / read_file_at) ───────────────────────────
uint8_t *flashfs_read_file(const char *game, const char *filename, size_t *out_size) {
    if (out_size) *out_size = 0;
    if (!mounted) return NULL;
    char path[96];
    snprintf(path, sizeof(path), "games/%s/%s", game, filename);
    struct lfs_file_config fc = {0};
    fc.buffer = g_fcache;
    lfs_file_t lf;
    if (lfs_file_opencfg(&lfs, &lf, path, LFS_O_RDONLY, &fc) < 0) return NULL;
    lfs_soff_t sz = lfs_file_size(&lfs, &lf);
    uint8_t *buf = (sz >= 0) ? (uint8_t *)malloc(sz ? sz : 1) : NULL;
    if (buf && sz > 0 && lfs_file_read(&lfs, &lf, buf, sz) != sz) { free(buf); buf = NULL; }
    lfs_file_close(&lfs, &lf);
    if (buf && out_size) *out_size = (size_t)sz;
    return buf;
}

size_t flashfs_read_at(const char *game, const char *filename,
                       size_t offset, void *buf, size_t len) {
    if (!mounted) return 0;
    char path[96];
    snprintf(path, sizeof(path), "games/%s/%s", game, filename);
    struct lfs_file_config fc = {0};
    fc.buffer = g_fcache;
    lfs_file_t lf;
    if (lfs_file_opencfg(&lfs, &lf, path, LFS_O_RDONLY, &fc) < 0) return 0;
    lfs_file_seek(&lfs, &lf, (lfs_soff_t)offset, LFS_SEEK_SET);
    lfs_ssize_t n = lfs_file_read(&lfs, &lf, buf, len);
    lfs_file_close(&lfs, &lf);
    return n > 0 ? (size_t)n : 0;
}

// ── Save games (/saves/<name>.sav) — streaming, mirrors sd_save_* ────────────
static lfs_file_t g_savefile;
static uint8_t    g_savecache[256];
static bool       g_save_open  = false;
static bool       g_save_write = false;

bool flashfs_save_open(const char *name, bool write) {
    if (!mounted) return false;
    if (g_save_open) flashfs_save_close();

    char path[64];
    snprintf(path, sizeof(path), "saves/%s.sav", name);
    struct lfs_file_config fc = {0};
    fc.buffer = g_savecache;
    int flags = write ? (LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) : LFS_O_RDONLY;

    if (write) {
        flashfs_write_lock();       // held until close
        lfs_mkdir(&lfs, "saves");   // ignore EEXIST
    }
    if (lfs_file_opencfg(&lfs, &g_savefile, path, flags, &fc) < 0) {
        if (write) flashfs_write_unlock();
        return false;
    }
    g_save_open = true;
    g_save_write = write;
    return true;
}

bool flashfs_save_write(const void *buf, size_t len) {
    if (!g_save_open || !g_save_write) return false;
    return lfs_file_write(&lfs, &g_savefile, buf, len) == (lfs_ssize_t)len;
}

size_t flashfs_save_read(void *buf, size_t len) {
    if (!g_save_open || g_save_write) return 0;
    lfs_ssize_t n = lfs_file_read(&lfs, &g_savefile, buf, len);
    return n > 0 ? (size_t)n : 0;
}

void flashfs_save_close(void) {
    if (!g_save_open) return;
    lfs_file_close(&lfs, &g_savefile);
    bool was_write = g_save_write;
    g_save_open = false;
    g_save_write = false;
    if (was_write) flashfs_write_unlock();
}
