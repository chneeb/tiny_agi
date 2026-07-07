#include "sdcard.h"
#include "kbd_input.h"
#include "lcdspi.h"
#include "flashfs.h"
#include "ff.h"
#include "sd_card.h"
#include "sd_spi.h"
#include "hw_config.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

// Required by FatFS when compiled without hardware RTC.
DWORD get_fattime(void) { return 0; }

static FATFS fs;

bool sd_card_init(void)
{
    sd_card_t *card = sd_get_by_num(0);
    if (!card) return false;
    FRESULT r = f_mount(&fs, "0:", 1);
    return r == FR_OK;
}

// Re-assert the SD's SPI baud before an SD access. Needed on shared-bus targets
// (restouch: LCD+SD on spi1) where the LCD resets the bus to its own 80 MHz on
// every draw; without this the next SD transfer runs at 80 MHz and fails. On
// dedicated-bus targets it just re-sets the same baud (harmless). Only valid
// after init (the low-freq init phase happens inside sd_card_init).
void sd_reclaim_bus(void)
{
    sd_card_t *card = sd_get_by_num(0);
    if (card) sd_spi_go_high_frequency(card);
}

#define MAX_GAMES 32
#define NAME_LEN  32

// Match main.c's default; CMakeLists overrides per target (picocalc=0).
#ifndef FLASHFS_ENABLED
#define FLASHFS_ENABLED 1
#endif

typedef struct {
    char name[NAME_LEN];
    bool on_sd;
    bool in_flash;
} game_entry_t;

static int find_game(const game_entry_t *g, int count, const char *name) {
    for (int i = 0; i < count; i++)
        if (strcasecmp(g[i].name, name) == 0) return i;
    return -1;
}

// Build the merged SD ∪ flash-cached game list into games[]; returns the count.
static int build_game_list(game_entry_t *games)
{
    int count = 0;

    // SD games (if a card is present).
    DIR dir;
    FILINFO fno;
    sd_reclaim_bus();
    if (f_opendir(&dir, "0:/agi") == FR_OK) {
        while (count < MAX_GAMES) {
            if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == '\0') break;
            if (!(fno.fattrib & AM_DIR)) continue;
            strncpy(games[count].name, fno.fname, NAME_LEN - 1);
            games[count].name[NAME_LEN - 1] = '\0';
            games[count].on_sd = true;
            games[count].in_flash = false;
            count++;
        }
        f_closedir(&dir);
    }

    // Merge in flash-cached games.
    char cached[MAX_GAMES][32];
    int nc = flashfs_list_games(cached, MAX_GAMES);
    for (int i = 0; i < nc; i++) {
        int idx = find_game(games, count, cached[i]);
        if (idx >= 0) {
            games[idx].in_flash = true;
        } else if (count < MAX_GAMES) {
            strncpy(games[count].name, cached[i], NAME_LEN - 1);
            games[count].name[NAME_LEN - 1] = '\0';
            games[count].on_sd = false;
            games[count].in_flash = true;
            count++;
        }
    }
    return count;
}

bool show_dir_chooser(game_choice_t *out)
{
    game_entry_t games[MAX_GAMES];
    int count = build_game_list(games);
    if (count == 0) {
        lcd_clear();
        lcd_print_string("No games (SD or flash)!\n");
        return false;
    }

    int sel = 0;
    bool redraw = true;
#if FLASHFS_ENABLED
    uint32_t fs_total = 0, fs_free = 0;
    flashfs_df(&fs_total, NULL, &fs_free);   // recomputed after R/D below
#endif

    while (1) {
        if (redraw) {
            lcd_clear();
            lcd_print_string("Select game:\n");
#if FLASHFS_ENABLED
            lcd_print_string("ENTER=play R=cache D=del\n");
            char fline[40];
            snprintf(fline, sizeof(fline), "Flash: %lu.%luM free / %luM\n\n",
                     (unsigned long)(fs_free / 1048576UL),
                     (unsigned long)((fs_free % 1048576UL) * 10UL / 1048576UL),
                     (unsigned long)(fs_total / 1048576UL));
            lcd_print_string(fline);
#else
            lcd_print_string("UP/DN  ENTER\n\n");
#endif
            for (int i = 0; i < count; i++) {
                const char *tag = games[i].on_sd
                    ? (games[i].in_flash ? " [SD+flash]" : " [SD]")
                    : " [flash]";
                char line[NAME_LEN + 16];
                snprintf(line, sizeof(line), "%s%s%s\n",
                         i == sel ? "> " : "  ", games[i].name, tag);
                lcd_print_string(line);
            }
            redraw = false;
        }

        int key = kbd_read();
        if (key < 0) continue;

        if (key == 0xB5) {                          // UP
            if (sel > 0) { sel--; redraw = true; }
        } else if (key == 0xB6) {                   // DOWN
            if (sel < count - 1) { sel++; redraw = true; }
        } else if (key == KB_ENTER_CODE) {           // ENTER = play (flash if cached, else SD)
            strncpy(out->name, games[sel].name, sizeof(out->name) - 1);
            out->name[sizeof(out->name) - 1] = '\0';
            out->on_sd = games[sel].on_sd;
            out->in_flash = games[sel].in_flash;
            out->refresh = false;
            return true;
        }
#if FLASHFS_ENABLED
        else if (key == 'r' || key == 'R') {         // R = cache/re-cache from SD (stay in menu)
            if (games[sel].on_sd) {
                lcd_clear();
                lcd_print_string("Caching to flash:\n  ");
                lcd_print_string(games[sel].name);
                lcd_print_string("\n\nScreen may go DARK.\nPlease wait, do not unplug.\n");
                sleep_ms(1800);   // readable before core1 (DVI) stops
                char sd[80];
                snprintf(sd, sizeof(sd), "0:/agi/%s", games[sel].name);
                bool cached = flashfs_cache_game(games[sel].name, sd);
                if (!cached) {
                    // Out of flash (or a mid-copy error) leaves a partial cache —
                    // delete it so the game doesn't show as [flash] but play broken.
                    flashfs_delete_game(games[sel].name);
                    lcd_clear();
                    lcd_print_string("Cache FAILED\n(flash full?)\n\n"
                                     "Partial cache removed.\n");
                    sleep_ms(2500);
                }
                count = build_game_list(games);       // refresh tags
                flashfs_df(&fs_total, NULL, &fs_free); // cache changed free space
            } else {
                lcd_clear();
                lcd_print_string("Not on SD - can't cache.\nInsert the SD card.\n");
                sleep_ms(1500);
            }
            redraw = true;
        }
        else if (key == 'd' || key == 'D') {         // D = delete flash cache (stay in menu)
            if (games[sel].in_flash) {
                flashfs_delete_game(games[sel].name);
                count = build_game_list(games);
                flashfs_df(&fs_total, NULL, &fs_free); // cache changed free space
                if (count == 0) {
                    lcd_clear();
                    lcd_print_string("No games left.\n");
                    sleep_ms(1200);
                    return false;
                }
                if (sel >= count) sel = count - 1;
            } else {
                lcd_clear();
                lcd_print_string("Not cached.\n");
                sleep_ms(900);
            }
            redraw = true;
        }
#endif  /* FLASHFS_ENABLED */
        else if (key == 0x1B || key == 0xB1) {      // ESC
            return false;
        }
    }
}

uint8_t *sd_read_file(const char *path, size_t *out_size)
{
    sd_reclaim_bus();
    FIL fil;
    FRESULT r = f_open(&fil, path, FA_READ);
    if (r != FR_OK) return NULL;

    FSIZE_t size = f_size(&fil);
    uint8_t *buf = malloc(size);
    if (!buf) { f_close(&fil); return NULL; }

    UINT br;
    r = f_read(&fil, buf, (UINT)size, &br);
    f_close(&fil);
    if (r != FR_OK || br != (UINT)size) { free(buf); return NULL; }

    *out_size = size;
    return buf;
}

void sd_free_file(uint8_t *buf)
{
    free(buf);
}

size_t sd_read_file_at(const char *path, size_t offset, void *buf, size_t len)
{
    sd_reclaim_bus();
    FIL fil;
    FRESULT r = f_open(&fil, path, FA_READ);
    if (r != FR_OK) return 0;
    f_lseek(&fil, (FSIZE_t)offset);
    UINT br = 0;
    f_read(&fil, buf, (UINT)len, &br);
    f_close(&fil);
    return (size_t)br;
}

static FIL save_fil;
static bool save_is_open = false;

bool sd_save_open(const char *path, bool write)
{
    sd_reclaim_bus();
    if (save_is_open) f_close(&save_fil);
    BYTE mode = write ? (FA_WRITE | FA_CREATE_ALWAYS) : FA_READ;
    FRESULT r = f_open(&save_fil, path, mode);
    save_is_open = (r == FR_OK);
    return save_is_open;
}

bool sd_save_write(const void *buf, size_t len)
{
    if (!save_is_open) return false;
    UINT bw;
    FRESULT r = f_write(&save_fil, buf, (UINT)len, &bw);
    return r == FR_OK && bw == (UINT)len;
}

bool sd_save_read(void *buf, size_t len)
{
    if (!save_is_open) return false;
    UINT br;
    FRESULT r = f_read(&save_fil, buf, (UINT)len, &br);
    return r == FR_OK && br == (UINT)len;
}

void sd_save_close(void)
{
    if (save_is_open) { f_close(&save_fil); save_is_open = false; }
}
