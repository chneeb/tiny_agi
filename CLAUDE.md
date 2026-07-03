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

tinyagi-rp2350/        Merged RP2350 port — PicoCalc and RESTOUCH as two CMake targets
  main.c               Shared game loop; #if SOUND_ENABLED / USE_VREG_BOOST guards
  platform.c           Shared platform callbacks; #if SOUND_ENABLED for audio path
  sdcard.c             Shared SD/FAT file I/O; KB_ENTER_CODE define per target
  display.h            Shared display interface (display_init, flush_display, etc.)
  audio/               PWM synth for AGI sound (linked by both; gated by SOUND_ENABLED)
  agi_sound_player/    AGI sound decoder (linked by both; gated by SOUND_ENABLED)
  fatfs/               FatFs_SPI submodule (shared)
  picocalc/            PicoCalc-specific hardware
    display.c          320×200 framebuffer → ILI9488 TFT (320×320, y-offset 60); RGB888
    kbd_input.*        PicoCalc keyboard driver (ClockworkPi i2ckbd library)
    hw_config.c        FatFs_SPI hw config for PicoCalc SD card
  restouch/            RESTOUCH-specific hardware
    display.c          320×200 framebuffer → ST7789 TFT (320×240, y-offset 20); RGB565
    lcdspi.*           ST7789 SPI driver; MADCTL=0xA0; 80 MHz; shared bus with SD
    kbd_input.*        M5Stack CardKB I2C driver (i2c1, GP2=SDA, GP3=SCL, addr 0x5F)
    hw_config.c        FatFs_SPI hw config: spi1, MISO=12, MOSI=11, SCK=10, CS=22
  dvi/                 DVI/HDMI target (Waveshare RP2350-PiZero) — third CMake target
    display.cpp        320×200 framebuffer → 640×480 DVI via pico_lib (C++); core1 scanout
    kbd_input.c        USB HID keyboard via PIO-USB host (tuh) + USB-C CDC stdio (tud)
    hw_config.c        FatFs_SPI hw config (SD on spi1, dedicated bus)
    tusb_config.h      TinyUSB config (host on rhport1/PIO, device on rhport0/native)
    usb_descriptors.c  CDC device descriptors
  pico_lib/            Shuichi Takano's pico DVI library (submodule) — PIO TMDS, NOT HSTX

tinyagi-glfw/          Desktop GLFW port (reference; not actively developed)

tools/
  agi_disasm.py        AGI Logic bytecode disassembler (Python 3, no dependencies)
```

## Build

Both targets are defined in a single CMakeLists.txt. Build from `tinyagi-rp2350/`:

```bash
cd tinyagi-rp2350
mkdir -p build && cd build
cmake .. -DPICO_SDK_PATH=/home/chneeb/Source/pico-sdk -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
# produces tinyagi_picocalc.uf2, tinyagi_restouch.uf2, and tinyagi_dvi.uf2
```

To build only one target: `make -j$(nproc) tinyagi_picocalc`, `… tinyagi_restouch`, or `… tinyagi_dvi`.

Requires Pico SDK 2.2.0, `PICO_BOARD=pico2` (RP2350 / Pico 2). The `tinyagi_dvi`
target is guarded by `if(EXISTS pico_lib/dvi/CMakeLists.txt)` — if the `pico_lib`
submodule isn't checked out, only picocalc/restouch build.

### Per-target compile definitions

| Define | PicoCalc | RESTOUCH | DVI |
|---|---|---|---|
| `SOUND_ENABLED` | 1 | 0 | 0 default (HDMI audio impl exists, off — open issue) |
| `KB_ENTER_CODE` | 0x0A | 0x0D | 0x0D |
| `KB_F10_CODE` | 0x90 | 0x8A | 0x8A |
| `SYS_CLOCK_KHZ` | 133000 | 300000 | 252000 (21×12 MHz, PIO-USB) |
| `USE_VREG_BOOST` | 0 | 1 | 1 |

DVI-only flags: `DVI_TARGET=1` (guards DVI code in shared main.c),
`DVI_ENABLE_CDC` (1 = USB-C serial console; 0 = UART-only),
`DVI_KEEPALIVE_TIMER=1` (workaround for the room-transition hang — see below),
`DVI_HDMI_AUDIO` (sound as HDMI data-island audio — impl works but **off by default**:
glitches audio-decoding monitors — see below), plus
`PICO_PIO_USE_GPIO_BASE=1`, `PICO_DEFAULT_PIO_USB_DP_PIN=28`.

## Hardware

### PicoCalc (`tinyagi_picocalc`)
- ClockworkPi PicoCalc — RP2350 (Cortex-M33), 520 KB SRAM, 4 MB flash
- ILI9488 SPI TFT, 320×320. AGI 320×200 frame centred (60-row black margins top/bottom).
- SPI1 runs at 50 MHz after `display_init()`.
- SD card holds game directories; each directory contains standard AGI VOL/DIR files.
- PWM audio on GP26.

### RESTOUCH (`tinyagi_restouch`)
- RP2350 (Pico 2) at 300 MHz (vreg boosted to 1.20 V).
- ST7789 SPI TFT, 320×240. AGI 320×200 frame centred (20-row black margins top/bottom).
- MADCTL=0xA0 (MY+MV for landscape). ENTER_INVERT_MODE needed for IPS panel colours.
- SPI1 shared by LCD (DC=8, CS=9, CLK=10, MOSI=11, RST=15, BL=13) and SD (CS=22, MISO=12).
- Touch CS (GP16) parked HIGH — touch not used.
- M5Stack CardKB on i2c1 (SDA=GP2, SCL=GP3, I2C addr 0x5F).
- No audio hardware; SOUND_ENABLED=0 stubs out the audio path at compile time.

### DVI (`tinyagi_dvi`) — Waveshare RP2350-PiZero (RP2350B)
- RP2350B at **252 MHz** (vreg boost 1.20 V). 252 = 21×12 MHz, required by PIO-USB.
- DVI via **pico_lib** (Shuichi Takano's PIO-based C++ DVI library), **NOT** HSTX. The
  original plan was HSTX; the working implementation uses pico_lib because it was
  already proven on this board (msx2pico). TMDS pins **GPIO 36/34/32** (Blue/Green/Red),
  clock **GPIO 38** — GPIOs 32-39 need `PICO_PIO_USE_GPIO_BASE=1` (RP2350B extended range).
- **Dual core**: core1 runs the DVI scanout loop (`core1_dvi_loop` in display.cpp) and owns
  `DMA_IRQ_0`; core0 runs the game. `flush_display()` is a **no-op** — core1 continuously
  scans the live `framebuffer[320*200]`. 640×480@60: each AGI pixel written 1:1 into the
  left 320 of a 640-wide line buffer (right half black); pico_lib `N_LINE_PER_DATA=2`
  line-doubles 240 scanlines → 480. (Horizontal is NOT full-screen — see known issues.)
- **Keyboard**: USB HID via **PIO-USB host** (`tuh`, pio1, D+ on GPIO 28). `tuh_task()` is
  pumped from `kbd_read()`.
- **Console**: USB-C **CDC device** (`tud`, native controller) — pico_stdio_usb is blocked
  when tinyusb_host is linked, so we register our own stdio driver (`cdc_stdio_init`).
  UART0 (GP0) stdio is also enabled.
- SD card on **spi1** (dedicated bus, no LCD sharing), `DMA_IRQ_1`. FatFs `set_spi_dma_irq_channel(true,true)`.
- Audio: no PWM DAC (GPIO23 is the RP2350-PiZero SMPS pin).  An **HDMI data-island audio**
  path exists (`DVI_HDMI_AUDIO`) and works, but is **off by default** — enabling it glitches the
  DVI signal on monitors that decode HDMI audio (open issue; see below).  When enabled it plays
  only through an HDMI sink that decodes audio; a pure DVI display stays silent.

## Key design decisions & fixes applied

### DVI: core0 stack relocated to main SRAM (critical)
The default RP2350 layout puts core0's stack in **SCRATCH_Y (only 4 KB)**, directly above
**SCRATCH_X**, which holds pico_lib's hand-written TMDS encode loops (`tmds_encode_loop_16bpp*`)
and the TMDS table. Deep AGI recursion (`call`/`new_room`) + FatFs + TinyUSB overflow 4 KB and
grow down into SCRATCH_X, **corrupting the encoder code core1 is executing** → core1 faults →
screen goes dark. Fix: `main()` (DVI only) switches MSP to a 32 KB `core0_stack[]` in main SRAM
before calling `agi_main()`, so core0's stack can never reach SCRATCH_X. This was the cause of
the original "screen goes dark after Starting:" crash. (msx2pico survives on 4 KB because fMSX
is iterative; AGI's logic recursion is deeper.)

### DVI: non-blocking USB-CDC output (critical)
`cdc_out_chars()` (dvi/kbd_input.c) must **never block**. It writes what fits in the TX buffer
and **drops the rest** — it must not spin waiting for the host to drain. An earlier blocking
version froze core0 inside `printf` under heavy debug output (every SD read printed). Corollary
for debugging: USB-CDC output only reaches the host while core0 pumps `tud_task()` (from
`kbd_read()`), so **any core0 hang kills the console** — a core0 crash looks completely silent
over USB-CDC. Use UART (GP0, 115200) to see output that survives a core0 hang; the fault handler
in display.cpp (`isr_hardfault`) prints there.

### DVI: room-transition hang — workaround in place, mechanism only partly pinned
Intermittent hang on room transitions (screen black, F7 dead, no `HARDFAULT` on UART = a *hang*,
not a fault). The wait involved: every SD block read (`spi_transfer` in the FatFs_SPI driver)
waits on its DMA via `sem_acquire_timeout_ms(1000)` → `best_effort_wfe_or_timeout` → **`__wfe`**,
released from the SD DMA-completion IRQ (`sem_release` → `__sev`) — a **cross-core, event/WFE-based**
wait the SDK itself calls best-effort ("the IRQ may be happening on the other core … callers must
poll in a loop"). Working theory: under sustained **core1 DVI-DMA** activity the cross-core wakeup
is intermittently lost → core0 rides the wait toward the timeout → `sem_acquire` fails → the block
read reports **failure** → the AGI engine gets a NULL/short resource → hang.
- **Keep-alive masks it** (`DVI_KEEPALIVE_TIMER=1`, main.c): a periodic timer IRQ wakes core0
  every ~250 ms (or ~2 ms via the HDMI-audio timer); on re-check the DMA *had* completed, so the
  read succeeds instead of failing. It never fixes the lost event — just re-polls often enough.
  A/B-confirmed: timer off = hang, timer on = stable.
- **CAVEAT — the mechanism above is NOT fully consistent.** PIO-USB runs its own **1 ms SOF
  repeating timer on core0** (its own alarm pool, HW alarm #2 — see `pio_usb_host.c` `start_timer`).
  That already wakes core0 every ~1 ms, so a plain "lost wakeup rides to the 1 s timeout" can't be
  literally right (something should wake it within 1 ms). The keep-alive is still empirically
  required, so the true trigger is subtler — likely in how `best_effort_wfe_or_timeout` arms/cancels
  default-pool alarms under SD-read churn, or an interaction between the two alarm pools. Not pinned.
- **`SEVONPEND` did NOT help** — it only makes a *pending interrupt* wake `__wfe`; here the
  interrupt fired, it's the cross-core *event* that's lost. Different failure.
- **A bigger/fuller/fragmented FAT partition amplifies it** (not causes it): more FAT/directory
  sector reads per transition = more `__wfe` waits = more chances to hit the intermittent race.
- **Real fix (TODO, not applied):** `dma_channel_wait_for_finish_blocking()` is a *pure hardware
  poll* (`while (dma_channel_is_busy) tight_loop_contents()`, no WFE) — immune to the race. Make
  `spi_transfer()` poll `dma_channel_is_busy(rx_dma)` with a `time_reached()` timeout instead of
  the semaphore, then the keep-alive can be removed. Caveat: `spi_transfer` is in the vendored
  `fatfs/FatFs_SPI` submodule, so this means patching/forking the submodule.

### DVI: HDMI data-island audio (`DVI_HDMI_AUDIO`)
AGI sound plays through the HDMI stream (monitor speakers) since there's no PWM DAC pin. Reuses
the AGI sequencer (`agi_sound.c` → `pwm_synth_channels[].hz`) unchanged; only the output stage
differs. `pwm_synth_render()` (audio/pwm_synth.c) mixes the 3 channels to signed int16 at 44100 Hz;
`dvi_audio_*()` (dvi/display.cpp) pushes mono→stereo into pico_lib's SPSC ring; a ~2 ms core0
timer (`audio_producer_cb`, main.c) tops the ring up, self-clocked to what core1 drained. Setup is
`setAudioFreq(44100,0,6144)` (auto-computes CTS, enables data islands) + `allocateAudioBuffer(1024)`.
Requires an **HDMI sink that decodes audio** — silent on pure DVI. **Off by default — open issue.**
Enabling audio glitches the DVI signal on **some monitors** (screen black, monitor re-syncs after
~10 s = signal loss). Debugging narrowed it down:
- **Not power** — reproduces on two known-good supplies.
- **DMA bus priority helped** (now always applied, see below) — it hardened the signal enough that
  the video-only monitor is solid with the audio build.
- **It's monitor-dependent**: a monitor with **no audio** works fine with audio enabled; a monitor
  that **decodes HDMI audio** still glitches. So the remaining cause is almost certainly the
  **audio data-islands themselves** — an audio-decoding sink rejects (drops sync) while a
  non-decoding sink ignores them.
- **Data-island spec fixes tried — did NOT resolve it.** Three genuine bugs were found and patched
  in `pico_lib/dvi/data_packet.cpp` + `dvi_audio_init()`, but the audio monitor still glitched, so
  they are **not the (whole) cause**. Reverted (kept the submodule pristine); left here as hints
  for whoever resumes — they're worth applying anyway, ideally as a **fork/PR to pico_lib** rather
  than editing the vendored submodule:
    1. `DataPacket::setAudioSample()`: the IEC-60958 block-start flag uses `frameCt < 4`; it should
       be `frameCt < n` (only flag a subpacket that actually carries a sample). With ~1–2 samples
       per packet this mis-flags most of the ~230 block boundaries/sec.
    2. Same function: `vuc = 1` marks every sample **invalid**; IEC-60958 validity is inverted —
       `0` = valid.
    3. `dvi_audio_init()`: N for 44.1 kHz should be **6272**, not 6144 (6144 is the 48 kHz value).
       At our exact 25.2 MHz pixel clock N=6272 gives CTS=28000 exactly (zero ACR error).
  Since these didn't fix it, the real cause may be data-island **timing/placement** (guard bands,
  packet scheduling under `N_LINE_PER_DATA=2` line-doubling) or something the specific sink is
  strict about — needs a capture/analyzer or a known-good HDMI-audio reference to pin down.
Mitigation for now: shipped off (`SOUND_ENABLED=0` + `DVI_HDMI_AUDIO=0`); code stays behind the flag.

Note: with `SOUND_ENABLED=1`, sound-done is signalled by the real path (`platform_tick_sound()`
sees `agi_sound_is_playing()` go false), replacing the earlier synchronous-completion hack that
was only needed when `SOUND_ENABLED=0`. That hack (`agi_play_sound()` `#else` branch) still exists
for the restouch target (`SOUND_ENABLED=0`), where a sound-wait would otherwise hang forever.

### DVI: DMA bus priority
`display_init()` sets `bus_ctrl_hw->priority = DMA_R | DMA_W` so the DVI scan-out DMA wins bus
arbitration over the cores. pico_lib doesn't do this; without it the scan-out DMA can be starved
by core-side SRAM traffic → TMDS underrun → the monitor loses sync (black ~10 s). Applied
unconditionally (hardens the signal in general); pico-infonesPlus does the same for RP2350 HDMI.

### DVI: display model (black-during-load + tearing)
core1 scans the live framebuffer with no double-buffering, so intermediate states during a room
load (e.g. `text_screen()` → `clear_lines(0,25,0)` blanks to black) are visible as a black screen
for the whole load, and sprite draws tear. PicoCalc hides this because its LCD only updates on the
per-cycle `flush_display()` push (a "hold" display). Fix (TODO): double-buffer — core1 scans a
front buffer, `flush_display()` copies the composed framebuffer into it once per cycle.

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

### Room transition: ego placement with 1-pixel buffer
`new_room()` (control_flow.c) places ego at the opposite side of the new room based on `VAR_2_EGO_BORDER_CODE`. The horizontal placements must offset by 1 pixel from the border, otherwise the border check re-trips on the next cycle and ego ping-pongs between the two rooms:
- `BORDER_LEFT` (came from left): `EGO.x = 159 - ego_cell->width` (NOT `160 - width`, which puts the sprite's right edge at the right border)
- `BORDER_RIGHT` (came from right): `EGO.x = 1` (NOT `0`, which is the left border)

The vertical placements already have natural buffers and don't need adjustment.

### Room transition: clamp ego.y below new horizon (post-cycle)
`new_room()` runs *before* the new room's logic sets its real horizon, so `EGO.y = state.horizon + 1` uses the default horizon (36). When the room then calls `set_horizon(85)` and only repositions ego's x (e.g. SQ1 room 19's entry handler for v1==16), ego is left above the new horizon (y=37 above horizon=85) and `update_all_active()` immediately triggers `BORDER_TOP`, bouncing ego back to the previous room.

Fix is in interpreter.c, after `execute_logic_cycle()` and before `update_all_active()`: if `FLAG_5_ROOM_EXECUTED_FIRST_TIME` is still set (i.e. the room just loaded), ego doesn't ignore horizon, and `EGO.y <= state.horizon`, clamp `EGO.y = state.horizon + 1`. This catches all four border directions in a single check — the original Sierra interpreter effectively did this by placing ego after the room logic ran.

### Rendering order: sprites drawn before logic (text on top)
`agi_draw_all_active()` is called inside `agi_logic_run_cycle()` (interpreter.c), **before** `execute_logic_cycle()`, not after. This means:

1. Sprites are drawn to the framebuffer at positions set by the previous cycle's `update_all_active()`.
2. `execute_logic_cycle()` runs — any `display()` or `print_at()` text calls write on top of the sprites.
3. `flush_display()` in `main.c` shows the composited result.

This matches the original AGI interpreter, which had a separate text overlay layer that always rendered above the graphics/sprite layer. The one-frame position lag introduced by drawing before updating is imperceptible at 20 fps.

Without this order, games that use `display()` for full-screen overlays without calling `text_screen()` (e.g. PQ1 newspaper, which uses `clear_text_rect()` + `display()` each cycle) would have the character sprite drawn on top of the text.

`_print()` also calls `agi_draw_all_active()` before showing a modal dialog box — with the new order this is a harmless redundant draw (sprites already on screen).

### text_mode (agi_text_mode global)
`text_screen()` sets `agi_text_mode = true` and `graphics()` sets it to `false` and calls `show_pic()`. When `agi_text_mode` is true, `agi_draw_all_active()` is skipped (guarded in interpreter.c), so the per-cycle erase-then-redraw of active views (which restores picture pixels from `pic_vis`, including doors) doesn't bleed background graphics through text-mode screens (e.g. SQ1 library cartridge research screen).

`agi_text_mode` is defined in display.c and declared in state.h as a free-standing global — **deliberately NOT a field of `agi_state_t`**. Adding it to the state struct changes `sizeof(agi_state_t)` and silently corrupts existing save files (the byte-aligned read in `restore_game()` shifts everything that follows). Keep transient UI state out of the serialised struct.

### restore_game() resets agi_text_mode
After `agi_save_data_read(file, &state, ...)`, `restore_game()` explicitly sets `agi_text_mode = false`. Saves can only be initiated from the input prompt (normal mode), so this is normally a no-op, but it's defensive against any future code path that could save while in text mode.

### AGI Logic disassembler
`tools/agi_disasm.py` disassembles AGI Logic bytecode. Usage:

```bash
python3 tools/agi_disasm.py <game_dir> <logic_no>
# e.g. python3 tools/agi_disasm.py ~/Downloads/quest/sq1 9
```

Decrypts messages with the "Avis Durgan" XOR key. Useful for understanding why a room behaves unexpectedly.

## Known issues / TODO

### DVI: room-transition hang — root-caused, workaround in place
Cross-core lost-wakeup in the SD `sem_acquire`/`__wfe` DMA wait under DVI-DMA contention (full
trace under "DVI: room-transition hang" in Key design decisions). Masked by `DVI_KEEPALIVE_TIMER`.
Real fix (make `spi_transfer` poll `dma_channel_is_busy` instead of the semaphore) is understood
but not applied — it means patching the vendored FatFs_SPI submodule.

### DVI: HDMI audio glitches audio-decoding monitors (off by default)
`DVI_HDMI_AUDIO=1` works (sound plays) but glitches the DVI signal on **monitors that decode HDMI
audio** (screen black, monitor re-syncs after ~10 s); a monitor with no audio is fine with the same
build. Ruled out power (two good supplies) and bus contention (the DMA bus-priority fix hardened the
non-audio case). Remaining suspect: the **audio data-islands** (CTS-N / audio InfoFrame / pico_lib
data-island spec compliance) which an audio sink rejects. Shipped **off**; code stays behind the
flag. Next: validate CTS/N against the ~25.2 MHz pixel clock + pico_lib's data-island output. Full
analysis under "DVI: HDMI data-island audio" in Key design decisions.

### DVI: horizontal is left-half only (not full-screen)
The 320-pixel AGI frame is written 1:1 into the left 320 of the 640-wide DVI line, so game
content occupies the left half with the right half black. True full-screen needs horizontal
2× (pixel-doubling into all 640), but naive doubling made the 8px chooser font "fat"; a clean
fix is a mode flag that doubles for the game but renders text 1:1, or a 640-wide text path.

### DVI: no double-buffer → black-during-load + tearing
See "DVI: display model" under Key design decisions. Fix is a front/back framebuffer swapped
in `flush_display()`.

### RESTOUCH: SPI clock drops to 12.5 MHz after SD card access
`lcdspi_init()` configures spi1 at 80 MHz. The FatFs_SPI library's `my_spi_init()` calls `spi_init(spi1, ...)` (one-time, guarded) then `spi_set_baudrate(spi1, 12.5 MHz)` on each SD access. After any SD read, spi1 is left at 12.5 MHz and all subsequent LCD operations (framebuffer flushes, startup text) run at that reduced rate. ST7789 is within spec at 12.5 MHz so rendering is correct but slower. Fix: call `spi_set_baudrate(LCD_SPI_PORT, LCD_SPI_CLOCK_HZ)` at the start of `lcdspi_set_address()`.

### Display overlay buffer (not yet implemented)
`display()` calls write text to the main framebuffer. On idle cycles (no keypress), the game logic may not redraw that text, but `agi_draw_all_active()` still runs and draws sprites over the retained text. This makes sprites visible over persistent display-based overlays such as the PQ1 newspaper (which uses `clear_text_rect()` + `display()` each cycle, but only when a key is pressed).

Fix: add a 40×25 character-cell overlay buffer (`char_overlay[25][40]`, `fg_overlay[25][40]`, `bg_overlay[25][40]`) in `display.c`. When `display()` writes a character, also record it in the overlay. When `clear_text_rect()` or `clear_lines()` clears an area, also clear those overlay cells. After `agi_draw_all_active()` in interpreter.c, call `apply_display_overlay()` to composite the overlay back over the framebuffer. Clear the overlay on `graphics()` and `new_room()`. ~50–60 lines across display.c, interpreter.c, control_flow.c. Adds ~3 KB of state.

Note: this is distinct from the `agi_text_mode` path — games that call `text_screen()` (e.g. SQ1 library) already suppress sprite drawing entirely and don't need the overlay.

### Sound on/off toggle is cosmetic (known)
`FLAG_9_SOUND_ENABLED` is read only in `display.c` (status line text) and `interpreter.c` (redraw trigger). The audio path in `commands/sound.c` does **not** check it — `sound()` always calls `agi_play_sound()` and currently-playing sounds aren't stopped when the flag flips. Toggling Sound Off via the menu / F2 updates the status line but the game keeps playing sounds. To fix: gate `sound()` on the flag (and still set the completion flag so logic waiting for sound-done doesn't hang), and call `agi_stop_sound()` when the flag transitions true→false (the interpreter already tracks `previous_sound_status`).

### restart_game() not implemented
Stubbed — will panic if called. Not yet triggered by SQ1/SQ2/PQ1.

### Memory leak (low priority)
`free_menu()` in state.c frees `menu_header_t` nodes but does not walk the `menu_item_t` chains inside each header — those are leaked on every `state_system_reset()`. Not a crash risk for typical session lengths.

## Implemented but stubbed AGI commands
The following return without doing anything; they don't crash:
`cancel_line`, `echo_line`, `hold_key`, `init_disk`, `init_joy`, `set_simple`, `toggle_monitor`

`restart_game` is also stubbed — calling it will panic. Not yet triggered by tested games.

## Save / restore
Implemented and working. No "Game saved" / "Game restored" dialog is shown — this is intentional.

## Tested games
- **Space Quest 1** — plays through correctly; menu, F1, quit, save/restore all work. Library computer terminal (text_screen mode) works. Typing "exit" at the terminal falls through silently (correct — word group 157 is not handled by the library logic; type an unrecognised word or press ESC to close the terminal cleanly).
- **Space Quest 2** — plays through correctly including intro sequence.
- **Police Quest 1** — partially tested. Newspaper room (Logic 116) renders correctly with the rendering-order fix; character sprite appears as a dark silhouette on idle frames (see display overlay buffer TODO). Exit via "close paper", "put down paper", or "stop reading".

Per-target notes: PicoCalc and RESTOUCH play the above. The **DVI** target boots, runs the
chooser, and plays SQ1 (walking, room transitions, save/restore) with `DVI_KEEPALIVE_TIMER=1`;
outstanding DVI items are the three known issues above (root-cause of the hang, full-screen
horizontal scaling, double-buffering).
