# tiny_agi — Claude Code context

## Repository layout

```
agi/                   Platform-independent AGI engine (C)
  src/
    interpreter.c      Main loop, input processing, get_message(), agi_initialize()
    state.c            state_reset(), state_system_reset(), free_menu/controller
    heap.c             Resource management: heap_reset(), heap_full_reset()
    commands/          One file per AGI command group
      control_flow.c   new_room(), call(), load_logics()
      system.c         quit(), save_game(), restore_game(), pause(), etc.
      display.c        print(), display(), text_screen(), graphics()
      object_view.c    Object movement, collision detection
      menu_io.c        set_menu(), set_menu_item(), menu_input()
  include/agi.h        Public API (agi_initialize, print_message_box)

tinyagi-picocalc/      RP2350 / PicoCalc port (the active target)
  main.c               Outer game-select loop + inner game loop
  platform.c           Platform callbacks: keyboard, display flush, sound, file I/O
  display.c            320×200 framebuffer → ILI9488 TFT (320×320, y-offset 60)
  kbd_input.*          PicoCalc keyboard driver
  sdcard.*             FAT SD card (game files live in per-game subdirectories)
  lcdspi.*             ILI9488 SPI driver
  audio/               PWM synth for AGI sound

tinyagi-glfw/          Desktop GLFW port (reference; not actively developed)

tools/
  agi_disasm.py        AGI Logic bytecode disassembler (Python 3, no dependencies)
```

## Build (PicoCalc target)

```bash
cd tinyagi-picocalc/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
# produces tinyagi_picocalc.uf2
```

Requires Pico SDK 2.2.0, `PICO_BOARD=pico2` (RP2350 / Pico 2).

## Hardware

- ClockworkPi PicoCalc — RP2350 (Cortex-M33), 520 KB SRAM, 4 MB flash
- ILI9488 SPI TFT, 320×320. AGI 320×200 frame is centred (60-row black margins top/bottom).
- SPI1 runs at 50 MHz after `display_init()`.
- SD card holds game directories; each directory contains standard AGI VOL/DIR files.

## Key design decisions & fixes applied

### Framebuffer layout
- `framebuffer[320 * 200]` — 1 byte per pixel, AGI colour index 0-15.
- `priority_buffer[160 * 168]` — 1 byte per cell.
- All four pixel/priority functions have `(unsigned)x >= W || (unsigned)y >= H` guards.

### Game directory selection
`show_dir_chooser()` in sdcard.c scans the SD root for directories and presents a list on the LCD. The selected path is stored in `game_dir[64]` (platform.c / main.c extern).

### Game loop (main.c)
Outer `while(1)` calls `show_dir_chooser()` then `agi_initialize()` and runs the inner game loop. When `state.game_state == STATE_QUIT` the inner loop breaks, `agi_stop_sound()` + `lcd_clear()` run, and the outer loop shows the chooser again.

### Quit
`quit(0)` shows "Quit game? (Y)es or (N)o" via `print_message_box` + `wait_for_key_yn()`. `quit(1)` exits immediately. Both set `state.game_state = STATE_QUIT`.

### Game switching / heap_full_reset()
`agi_initialize()` calls `heap_full_reset()` before `state_reset()`. This frees **all** 256 logic/pic/view/sound slots (including logic 0 — `heap_destroy_resources()` skipped it), frees and NULLs `item_file` and `words_file` so they are reloaded from the new game directory on the next `new_room()`, and resets the script buffer. Without this, the second game ran the first game's logic 0 and used its item/vocabulary files.

### Input
- **ESC** (ascii 27) opens the menu via `menu_input()` in `process_input_game()`. Inside `process_input_menu()`, ESC also closes the menu.
- **F1** falls through to the controller-assignment lookup so game scripts assign it to Help as intended. It is NOT hardcoded to `menu_input()`.
- **F3** recalls the previous input line.

Do not remap F1 back to `menu_input()`. The original Sierra AGI convention is ESC = menu, F1 = Help controller.

### enter_pressed lifecycle
`state.enter_pressed` is set by `check_key()` when the user presses Enter during normal gameplay. It must be cleared in two places:

1. **After `execute_logic_cycle()`** (interpreter.c, inside the STATE_PLAYING block) — prevents the command-submission Enter from persisting into subsequent cycles, which would cause the interpreter to repeatedly clear the input buffer and make typing impossible.
2. **After `close_menu()`** in the STATE_MENU handler — prevents the menu-selection Enter from leaking into `have_key()` inside a triggered logic (e.g. the Help/About screen), which would cause it to exit immediately.

`wait_for_enter()` in platform.c must NOT set `enter_pressed = true`. It consumes the keypress directly via `kbd_read()` (bypassing the game loop keyboard handler); if it also set `enter_pressed`, the flag would be seen by the STATE_MENU handler on the next cycle and auto-fire the current menu item.

### have_key() — flush and keyboard polling
Some AGI logics implement wait-for-keypress as a tight `goto` loop in bytecode that calls `have_key()` repeatedly without ever returning to the main game loop. This means `check_key()` in `main.c` never runs, so `enter_pressed` is never set and the interpreter deadlocks. `have_key()` in `test.c` therefore:
- Calls `platform_flush_display()` on the first poll (so the help screen is visible before the user presses a key).
- Calls `check_key()` on every poll while waiting (so keyboard input is processed inside the tight loop).

`check_key()` is declared in `platform_support.h` with a note that it is designed for exactly this use.

### player_control() restores text input
`player_control()` calls `accept_input()` so that text input is re-enabled whenever movement control returns to the player — whether from a script command or from `update_object()` completing a `move_obj()` animation. Without this, any room logic that calls `prevent_input()` before a walk (e.g. SQ1 vehicle bay Logic 8) leaves the player unable to type after the walk finishes, because `accept_input()` is never called by the game logic itself.

### new_room() clears FLAG_3 (ego touched trigger)
`new_room()` explicitly resets `FLAG_3_EGO_TOUCHED_TRIGGER`. Without this, the flag set by `update_all_active()` in the previous room's cycle carries over, causing trigger-zone death checks in the new room's logic to fire immediately on the first cycle (e.g. SQ1 vehicle bay room 8 death when entering from room 9).

### get_message() bounds guard
`message_no` is 1-indexed; passing 0 causes a `uint16_t` underflow to 65535, producing a wild pointer. Guard: `if (message_no >= message_section[0]) return "";`

### new_room() cleanup
Clears `system_state.input_buffer` / `input_pos` so text entered in one room (or via `parse()`) doesn't bleed into the next room's prompt.

### move_obj() restores player control on completion
`move_obj()` for ego (object 0) calls `program_control()` when the move starts. When the move completes, `player_control()` is called automatically (`object_view.c`, inside `update_object()`). This matches the AGI spec. Without this, Roger would be permanently frozen after any animated room-entry walk (e.g. SQ1 room 9).

### AGI Logic disassembler
`tools/agi_disasm.py` disassembles AGI Logic bytecode. Usage:

```bash
python3 tools/agi_disasm.py <game_dir> <logic_no>
# e.g. python3 tools/agi_disasm.py ~/Downloads/quest/sq1 9
```

Decrypts messages with the "Avis Durgan" XOR key. Useful for understanding why a room behaves unexpectedly.

### Memory leak (known, low priority)
`free_menu()` in state.c frees `menu_header_t` nodes but does not walk the `menu_item_t` chains inside each header — those are leaked on every `state_system_reset()`. Not a crash risk for typical session lengths.

## Implemented but stubbed AGI commands
The following return without doing anything; they don't crash:
`cancel_line`, `echo_line`, `hold_key`, `init_disk`, `init_joy`, `set_simple`, `toggle_monitor`

`restart_game` is also stubbed — calling it will panic. Not yet triggered by tested games.

## Save / restore
Implemented and working. No "Game saved" / "Game restored" dialog is shown — this is intentional.

## Tested games
- **Space Quest 1** — plays through correctly; menu, F1, quit, save/restore all work.
- **Space Quest 2** — plays through correctly including intro sequence.
