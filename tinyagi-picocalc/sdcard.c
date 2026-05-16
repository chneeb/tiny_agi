#include "sdcard.h"
#include "kbd_input.h"
#include "lcdspi.h"
#include "ff.h"
#include "sd_card.h"
#include "hw_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Required by FatFS when compiled without hardware RTC.
DWORD get_fattime(void) { return 0; }

static FATFS fs;

bool sd_card_init(void) {
    sd_card_t *card = sd_get_by_num(0);
    if (!card) return false;
    FRESULT r = f_mount(&fs, "0:", 1);
    return r == FR_OK;
}

#define MAX_GAMES 32
#define NAME_LEN  32

bool show_dir_chooser(char *out_path, size_t len) {
    char names[MAX_GAMES][NAME_LEN];
    int count = 0;

    DIR dir;
    FILINFO fno;
    FRESULT r = f_opendir(&dir, "0:/agi");
    if (r != FR_OK) {
        lcd_clear();
        lcd_print_string("No /agi dir on SD card!\n");
        return false;
    }
    while (count < MAX_GAMES) {
        r = f_readdir(&dir, &fno);
        if (r != FR_OK || fno.fname[0] == '\0') break;
        if (!(fno.fattrib & AM_DIR)) continue;
        strncpy(names[count], fno.fname, NAME_LEN - 1);
        names[count][NAME_LEN - 1] = '\0';
        count++;
    }
    f_closedir(&dir);

    if (count == 0) {
        lcd_clear();
        lcd_print_string("No games in /agi!\n");
        return false;
    }

    int sel = 0;
    bool redraw = true;

    while (1) {
        if (redraw) {
            lcd_clear();
            lcd_print_string("Select game (UP/DN/ENTER):\n\n");
            for (int i = 0; i < count; i++) {
                char line[NAME_LEN + 4];
                snprintf(line, sizeof(line), "%s%s\n", i == sel ? "> " : "  ", names[i]);
                lcd_print_string(line);
            }
            redraw = false;
        }

        int key = kbd_read();
        if (key < 0) continue;

        if (key == 0xB5) {          // UP
            if (sel > 0) { sel--; redraw = true; }
        } else if (key == 0xB6) {   // DOWN
            if (sel < count - 1) { sel++; redraw = true; }
        } else if (key == 0x0A) {   // ENTER
            snprintf(out_path, len, "0:/agi/%s", names[sel]);
            return true;
        } else if (key == 0xB1) {   // ESC
            return false;
        }
    }
}

uint8_t *sd_read_file(const char *path, size_t *out_size) {
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

void sd_free_file(uint8_t *buf) {
    free(buf);
}

size_t sd_read_file_at(const char *path, size_t offset, void *buf, size_t len) {
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
static bool save_open = false;

bool sd_save_open(const char *path, bool write) {
    if (save_open) f_close(&save_fil);
    BYTE mode = write ? (FA_WRITE | FA_CREATE_ALWAYS) : FA_READ;
    FRESULT r = f_open(&save_fil, path, mode);
    save_open = (r == FR_OK);
    return save_open;
}

bool sd_save_write(const void *buf, size_t len) {
    if (!save_open) return false;
    UINT bw;
    FRESULT r = f_write(&save_fil, buf, (UINT)len, &bw);
    return r == FR_OK && bw == (UINT)len;
}

bool sd_save_read(void *buf, size_t len) {
    if (!save_open) return false;
    UINT br;
    FRESULT r = f_read(&save_fil, buf, (UINT)len, &br);
    return r == FR_OK && br == (UINT)len;
}

void sd_save_close(void) {
    if (save_open) { f_close(&save_fil); save_open = false; }
}
