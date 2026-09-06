#pragma once

/* Shared between main.c and platform.c. */
extern char game_dir[512];   /* directory the game's resources live in */
extern char save_path[512];  /* save-game file path */

void platform_scan_game_dir(void);  /* (re)build the case-insensitive name map */
