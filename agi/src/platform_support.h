#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

extern void panic(const char* fmt, ...);

typedef struct {
	uint8_t* data;
	size_t size;
} agi_file_t;

extern agi_file_t get_file(const char* filename);
extern void free_file(agi_file_t file);

// Read len bytes from filename starting at byte offset (avoids loading whole VOL files).
extern size_t read_file_at(const char* filename, size_t offset, uint8_t* buf, size_t len);

extern void screen_set_160(int x, int y, int color);
extern void screen_set_320(int x, int y, int color);
extern int priority_get(int x, int y);
extern void priority_set(int x, int y, int priority);

extern void check_key();
extern void wait_for_enter();
extern bool wait_for_key_yn(void);
extern void platform_debug_flush(void);

// Flush framebuffer to display and tick sound. Call during blocking engine loops.
extern void platform_flush_display(void);

// Advance AGI sound by the correct number of 1/60s ticks based on real elapsed time.
extern void platform_tick_sound(void);

extern void agi_shake_screen(uint8_t times);

extern void agi_play_sound(uint8_t* sound_data);
extern void agi_stop_sound();

extern uint8_t font_data[];

typedef void* agi_save_data_file_ptr;
agi_save_data_file_ptr agi_save_data_open(const char* mode);
void agi_save_data_write(agi_save_data_file_ptr file_ptr, void* data, size_t size);
void agi_save_data_read(agi_save_data_file_ptr file_ptr, void* destination, size_t size);
void agi_save_data_close(agi_save_data_file_ptr file_ptr);