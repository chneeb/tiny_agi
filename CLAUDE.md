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
  sdcard.c             Shared SD/FAT file I/O + chooser (merges SD & flash-cached games)
  flashfs.c/.h         Flash game/save cache on littlefs (fixes DVI blanking; FLASHFS_ENABLED)
  display.h            Shared display interface (display_init, flush_display, etc.)
  audio/               PWM synth for AGI sound (linked by both; gated by SOUND_ENABLED)
  agi_sound_player/    AGI sound decoder (linked by both; gated by SOUND_ENABLED)
  fatfs/               FatFs_SPI submodule (shared)
  littlefs/            littlefs (vendored) — backs the flash game/save cache (shared)
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
    display.cpp        320×200 framebuffer → 640×480 DVI via pico_lib (C++); core1 scanout, double-buffered
    kbd_input.c        USB HID keyboard via PIO-USB host (tuh) + USB-C CDC stdio (tud)
    hw_config.c        FatFs_SPI hw config (SD on spi1, dedicated bus)
    sd_spi_poll.c      Patched copy of FatFs_SPI spi.c — polls DMA instead of the sem/WFE wait (DVI only)
    tusb_config.h      TinyUSB config (host on rhport1/PIO, device on rhport0/native)
    usb_descriptors.c  CDC device descriptors
  patches/             Standalone patches not applied to the tree (e.g. dircache_faster_loads.patch)
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

### RP2040-PiZero DVI target (fourth target — separate build)

`PICO_BOARD`/`PICO_PLATFORM` is global to a configure, so the RP2040 target can't
coexist with the three RP2350 (pico2) targets in one build — it needs its **own build
dir** with `-DTINYAGI_RP2040_PIZERO=ON`:

```bash
cd tinyagi-rp2350
mkdir -p build_rp2040 && cd build_rp2040
cmake .. -DPICO_SDK_PATH=/home/chneeb/Source/pico-sdk -DCMAKE_BUILD_TYPE=Release -DTINYAGI_RP2040_PIZERO=ON
make -j$(nproc)          # produces tinyagi_dvi_rp2040.uf2 only
```

The option flips `PICO_BOARD` to `pico` (RP2040/M0+) and builds **only** `tinyagi_dvi_rp2040`
(the pico2 targets are guarded out). This target **reuses the entire `dvi/` port** — the
board-specific bits are switched by the `RP2040_PIZERO=1` compile def inside `dvi/display.cpp`
(TMDS pins), `dvi/hw_config.c` (SD bus), `dvi/kbd_input.c` (USB), and `dvi/tusb_config.h` (USB
roles). **Motivation**: the RP2040-PiZero routes +5 V to its mini-HDMI connector (HDMI pin 18),
so its DVI works on **regular TVs**; the RP2350-PiZero does not (see the pin-18 note under Known
issues). Status: **working on RP2040 hardware** (SD menu, flash caching, SQ1/SQ2 play).

### Per-target compile definitions

| Define | PicoCalc | RESTOUCH | DVI |
|---|---|---|---|
| `SOUND_ENABLED` | 1 | 1 (I2S DAC) | 1 (HDMI data-island audio) |
| `KB_ENTER_CODE` | 0x0A | 0x0D | 0x0D |
| `KB_F10_CODE` | 0x90 | 0x8A | 0x8A |
| `SYS_CLOCK_KHZ` | 133000 | 300000 | 252000 (21×12 MHz, PIO-USB) |
| `USE_VREG_BOOST` | 0 | 1 | 1 |
| `FLASHFS_ENABLED` | 0 (SD-direct) | 1 | 1 (default) |
| `PICO_FLASH_SIZE_BYTES` | 4 MB (default) | 4 MB (default) | 16 MB (override) |

`FLASHFS_ENABLED` (default 1) turns on the flash game/save cache (see "Flash game/save
cache" below). `=0` reverts to playing directly from SD (SD required). picocalc ships with
it **off** (plays from SD), restouch/DVI **on**. DVI overrides `PICO_FLASH_SIZE_BYTES` to
16 MB (the RP2350-PiZero's real size; pico2 default is 4 MB) so littlefs uses the full region.

DVI-only flags (current defaults): `DVI_TARGET=1` (guards DVI code in shared main.c),
`DVI_ENABLE_CDC=0` (1 = USB-C serial console; 0 = UART-only),
`DVI_KEEPALIVE_TIMER=1` (secondary belt for the room-transition hang — see below),
`DVI_DOUBLE_BUFFER=1` (core1 scans a front buffer; flush copies once/cycle — see below),
`DVI_HDMI_AUDIO=1` (sound as HDMI data-island audio — **on by default now**, but see the
blanking/audio open issues below), `DVI_DBG_TIMER=0` (UART failure-mode diagnostic, off),
plus `PICO_PIO_USE_GPIO_BASE=1`, `PICO_DEFAULT_PIO_USB_DP_PIN=28`. The DVI target also
compiles a **patched SD driver** (`dvi/sd_spi_poll.c`) instead of the stock `spi.c` — see
"DVI: SD read via DMA poll" below. `DVI_DMA_BUS_PRIORITY` was tried and **removed** (no help;
A/B'd marginally worse, including with audio).

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
- Audio via an **external I2S DAC** (e.g. a Waveshare Pico Audio shield): DIN=GP26, BCK=GP27,
  LRCK=GP28. `SOUND_ENABLED=1` + `I2S_AUDIO=1` (was silent). Uses PIO0 + 2 DMA channels, fully
  independent of the LCD (hardware spi1) — see "I2S audio output". Confirmed on hardware. Without a
  DAC connected it just drives those pins harmlessly.

### DVI (`tinyagi_dvi`) — Waveshare RP2350-PiZero (RP2350B)
- RP2350B at **252 MHz** (vreg boost 1.20 V). 252 = 21×12 MHz, required by PIO-USB.
- DVI via **pico_lib** (Shuichi Takano's PIO-based C++ DVI library), **NOT** HSTX. The
  original plan was HSTX; the working implementation uses pico_lib because it was
  already proven on this board (msx2pico). TMDS pins **GPIO 36/34/32** (Blue/Green/Red),
  clock **GPIO 38** — GPIOs 32-39 need `PICO_PIO_USE_GPIO_BASE=1` (RP2350B extended range).
- **Dual core**: core1 runs the DVI scanout loop (`core1_dvi_loop` in display.cpp) and owns
  `DMA_IRQ_0`; core0 runs the game. With `DVI_DOUBLE_BUFFER=1` (default) core1 scans a
  dedicated `scanout_buffer[320*200]` and `flush_display()` `memcpy`s the composed
  `framebuffer` into it once per cycle (so only completed frames show — no black flash
  mid-load, less tearing). 640×480@60: each AGI pixel written 1:1 into the
  left 320 of a 640-wide line buffer (right half black); pico_lib `N_LINE_PER_DATA=2`
  line-doubles 240 scanlines → 480. (Horizontal is NOT full-screen — see known issues.)
- **Keyboard**: USB HID via **PIO-USB host** (`tuh`, pio1, D+ on GPIO 28). `tuh_task()` is
  pumped from `kbd_read()`.
- **Console**: USB-C **CDC device** (`tud`, native controller) — pico_stdio_usb is blocked
  when tinyusb_host is linked, so we register our own stdio driver (`cdc_stdio_init`).
  UART0 (GP0) stdio is also enabled.
- SD card on **spi1** (dedicated bus, no LCD sharing), `DMA_IRQ_1`. FatFs `set_spi_dma_irq_channel(true,true)`.
- Audio: no PWM DAC (GPIO23 is the RP2350-PiZero SMPS pin).  Sound plays via an **HDMI
  data-island audio** path (`DVI_HDMI_AUDIO`, `SOUND_ENABLED`), **on by default now**. It
  plays only through an HDMI sink that decodes audio; a pure DVI display stays silent. Caveat:
  on **audio-decoding** monitors it can still glitch the DVI signal (see open issues) — the
  bus-priority hardening that used to mask that has been removed, so if a specific audio
  monitor mis-behaves, building with `SOUND_ENABLED=0 DVI_HDMI_AUDIO=0` is the fallback.

### DVI on RP2040 (`tinyagi_dvi_rp2040`) — Waveshare RP2040-PiZero
Same `dvi/` port, `RP2040_PIZERO=1` switches the board-specific bits. pico_lib supports RP2040
(uses the interpolator/LUT TMDS encoder, `DVI_USE_SIO_TMDS_ENCODER=0`, since RP2040 has no SIO
TMDS encoder). Differences vs the RP2350-PiZero:
- **TMDS pins GPIO 26/24/22 (Blue/Green/Red), clock 28** (`dvi/display.cpp`) — all ≤29, so **no**
  `PICO_PIO_USE_GPIO_BASE`. 252 MHz, vreg 1.20 V (same). 640×480@60, RGB555 (same).
- **Keyboard: native USB host** on rhport 0 (`tuh_init(0)`), **not** PIO-USB — because GPIO 28 is
  the DVI clock here (PIO-USB D+ sat on 28 on RP2350). So no PIO-USB, and **no USB-CDC console**
  (native controller is the host; console is UART only, `DVI_ENABLE_CDC=0`). Proven approach:
  msxemulator does the same on this board. `dvi/kbd_input.c`/`tusb_config.h` guard on `RP2040_PIZERO`.
- **SD on spi0**: SCK=18, MOSI=19, MISO=20, CS=21 (onboard microSD; `dvi/hw_config.c`).
- **4-bit packed double buffer (`DVI_PACKED_FB=1`, RP2040 only)**: RP2040's 264 KB SRAM has no room
  for an 8-bit scanout buffer (hence `DVI_DOUBLE_BUFFER=0`). Instead the framebuffer stores **2
  pixels/byte** (4-bit — AGI is 16 colours, lossless), so framebuffer (32 KB) + scanout (32 KB) =
  64 KB — the *same* RAM one 8-bit framebuffer used. This gives **double-buffering (no flicker) at
  zero extra RAM**: `flush_display()` packs+copies fb→scanout once per cycle; core1 unpacks 2 px/byte
  in its scan loop (**half** the SRAM reads of 8-bit, so ~neutral on its tight timing — this is why
  it fits where HDMI audio didn't). `screen_set_320`/`lcd_putchar` do a nibble read-modify-write;
  `screen_set_160` writes a full byte (both nibbles). All gated `#if DVI_PACKED_FB` in
  `dvi/display.cpp` (with a `fb_put()` helper) — **RP2350 keeps its 8-bit double buffer untouched**
  (`DVI_PACKED_FB=0` → the `#else` original paths). Confirmed on RP2040 *and* RP2350 hardware.
- To claw back heap, `core0_stack` is trimmed 32→20 KB and `core1_stack` 8→4 KB **on RP2040 only**
  (`#if RP2040_PIZERO`). That leaves **~77 KB AGI heap**; static use ~180 KB of the 256 KB striped
  region (fb 32 KB + scanout 32 KB + priority 27 KB + `pic_vispri` 27 KB + stacks + libs).
- **Heap is tight but workable**: SQ1/SQ2/PQ1 play from the flash cache. It can still OOM on the
  heaviest rooms (failed resource `malloc` → `panic()` → core0 halts, core1 freezes the frame). The
  packing is spent on double-buffering (RAM-neutral), so it didn't free heap; the two 27 KB priority
  buffers are **not** a free reclaim either (`pic_vispri` is already vis+pri packed; `priority_buffer`
  composites sprite+text priority and stores 255 for text). Further heap would need smaller stack
  trims or engine-side resource eviction.
- **No HDMI audio** (`SOUND_ENABLED=0`, `DVI_HDMI_AUDIO=0`) — the sound *sequencer* still runs for
  timing (see "Sound timing decoupled…"), so sound-paced logic is correct but silent. **HDMI audio
  was tried and does NOT work on RP2040**: enabling it broke the DVI signal (background went red +
  flickering = per-scanline timing missed), because RP2040's core1 does software interp+LUT TMDS
  (no SIO encoder) and has no slack for the extra data-island work. So HDMI audio is an
  RP2350-only feature. The viable path here is **PWM audio on a spare GPIO** (e.g. GPIO6 like
  msxemulator) — ~0 RAM, core0/IRQ-driven so no core1 impact — but it needs an external
  speaker/amp (not the TV). Not yet implemented.
- **Flicker: FIXED** by the 4-bit packed double buffer above — core1 never scans a half-drawn frame.
  (Historical: a **beam-sync attempt** — `platform_frame_sync()` parking core0 until the beam left
  the content rows — was tried first and **reverted**; it helped flicker slightly but made the
  **PQ1 character stop rendering** (SQ1 was fine). Don't reintroduce it; the packed double buffer is
  the right fix.)
- **`PICO_FLASH_SIZE_BYTES=16 MB`** (the RP2040-PiZero's real size; littlefs cache = 15 MB, ~28
  games at ~0.5 MB each). Keeps the DMA-poll SD driver + keep-alive timer.
- **Status: works on RP2040 hardware** — SD menu, cache-to-flash, and playing SQ1/SQ2 from flash
  all confirmed. Console is UART only (`panic()` → UART).

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

### DVI: room-transition hang — DMA-poll SD driver applied + keep-alive kept
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
- **Real fix — APPLIED (`dvi/sd_spi_poll.c`).** `spi_transfer()` now polls the DMA hardware
  BUSY bit (`while (dma_channel_is_busy(rx_dma)) { if time_reached(deadline) fail; tight_loop_contents(); }`)
  instead of the semaphore/`__wfe` wait — a pure hardware poll, immune to the lost-wakeup. Because
  `spi_transfer` lives in the vendored `fatfs/FatFs_SPI` submodule, this is done **without editing
  the submodule**: `dvi/sd_spi_poll.c` is a patched *copy* of the driver's `spi.c` (only the wait
  changed, ~15 lines), swapped in for the DVI target only via the CMakeLists per-target `spi.c`
  split; picocalc/restouch keep the stock file. (Silent-divergence caveat: a future FatFs update
  to `spi.c` won't reach the copy.)
- **Both the poll driver AND the keep-alive are kept.** The poll driver is the root fix, but A/B
  on device showed the DVI is **much worse with the poll driver removed** (keep-alive alone is not
  enough), so the fork earns its place; the keep-alive stays as a cheap secondary belt. `DVI_DMA_BUS_PRIORITY`
  was also tried here and **removed** — no help, marginally worse (see below).

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

### Sound timing decoupled from audio output (all targets)
The AGI sound **sequencer** (`agi_sound_player/agi_sound.c`) runs on **every** target, even when
audio output is off. `SOUND_ENABLED` now gates only the audio **output** (PWM on picocalc, HDMI
data-island on DVI, I2S DAC on restouch); the sequencer's **timing** always runs. This matters because
sound-paced game logic keys off the sound-done flag: e.g. the SQ1 intro plays a sound arming flag
162 and shows each title card (incl. "Sarien Encounter") **while that sound is playing** (`f162`
still false), advancing only when it ends. `platform_tick_sound()` (called every cycle from
`main.c`, `platform_flush_display()`, `wait_for_enter()`) advances the sequencer and sets the
completion flag at the sound's **real** end. On no-output targets the sequencer just updates the
`pwm_synth_channels[]` frequency table (which nothing plays) — silent, but correctly timed.
- This **replaced an instant-completion hack** in `agi_play_sound()` that (on `SOUND_ENABLED=0`)
  set the sound-done flag immediately. That made sounds "finish" the instant they started, so the
  "while sound playing" window never existed → the intro's title cards never showed and the state
  machine raced/desynced. Now restouch times sounds exactly like the sound-on boards, just muted.
- Safe for picocalc/DVI: for `SOUND_ENABLED=1` the ungated paths are the code that already ran,
  so those targets are behaviorally unchanged (the HDMI-audio output path is untouched).
- The sequencer is pure data (no hardware): `agi_sound_start/tick` only touch `pwm_synth_channels[]`
  and `pwm_synth_silence_all_channels()` (zeroes the table) — safe without `pwm_synth_init()`.

### I2S audio output (restouch — external DAC)
`audio/i2s_output.c` + `audio/audio_i2s.pio` drive an external I2S DAC (a Waveshare Pico Audio
shield: **DIN=GP26, BCK=GP27, LRCK=GP28**). Gated by `I2S_AUDIO=1` (+`SOUND_ENABLED=1`); `main.c`'s
audio-init picks it via `#elif I2S_AUDIO`. Reuses the shared pipeline unchanged — the sequencer
sets `pwm_synth_channels[]`, `pwm_synth_render()` mixes to mono int16; `i2s_output` duplicates
mono→L/R into 32-bit I2S words. Structure:
- **PIO0 SM** runs the vendored `audio_i2s.pio` (RPi BSD; 16-bit stereo, side-set BCLK/LRCK).
  Clkdiv `sysclk*4/rate` (msxemulator's proven formula); 22050 Hz.
- **Two DMA channels chain to each other** (gapless ping-pong) with a read-address **ring** wrapping
  each 256-sample (1 KB, aligned) buffer. A 4 ms repeating timer tops up whichever buffer just went
  idle (buffers are ~11.6 ms, so it catches every swap). No DMA IRQ → no contention with SD.
- **Fully independent of the display** (LCD is hardware spi1; both PIOs were free) — audio can't
  disturb the picture. Uses `pio_claim_unused_sm` / `dma_claim_unused_channel`, so no fixed-resource
  clashes. Confirmed on hardware.
- Respects the Sound on/off toggle for free (`pwm_synth_render()` returns silence when FLAG_9 muted).
- Ported from msxemulator's I2S (same DAC scheme on the RP2040-PiZero). Could also serve DVI/RP2040
  or picocalc if a DAC is wired (it's a generic `I2S_AUDIO` output stage), but only enabled on
  restouch for now.
- **Known: AGI sound tempo feels slightly slow** — but this is **not** I2S-specific (observed on
  other targets too), so it's a shared **sequencer-tempo** question (`agi_sound.c` / the 60 Hz tick
  in `platform_tick_sound()`), not an output-path issue. Not yet investigated.

### DVI: DMA bus priority — tried and REMOVED
`display_init()` used to set `bus_ctrl_hw->priority = DMA_R | DMA_W` (DVI scan-out DMA wins bus
arbitration over the cores) to harden the signal against core-side SRAM traffic. **Removed** after
on-device A/B: it made **no positive difference** and was **marginally worse** — because it also
makes core0's **SD DMA outrank core1's software-TMDS fill loop**. Tested both with audio off and on
(the audio case was the original reason it was added); neither benefited. The code is gone, not just
flag-gated. If a future audio-decoding-monitor issue needs it back, it was
`bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS`.

### DVI: display model — double-buffered (implemented)
`DVI_DOUBLE_BUFFER=1` (default): core1 scans a dedicated `scanout_buffer`, and `flush_display()`
`memcpy`s the composed `framebuffer` into it once per cycle. So intermediate states during a room
load are no longer shown and sprite draws don't tear — same "hold" behaviour PicoCalc gets from its
per-cycle LCD push. Costs +64 KB SRAM. `DVI_DOUBLE_BUFFER=0` reverts to live-scan of the framebuffer
(A/B'd **much worse** — core1 reads the framebuffer while core0 writes it, and it dies fast). Note
this does **not** fix the room-transition *signal blanking* — that's a separate, deeper issue (see
"DVI: room-transition signal blanking" under Known issues).

### Framebuffer layout
- `framebuffer[320 * 200]` — 1 byte per pixel, AGI colour index 0-15.
- `priority_buffer[160 * 168]` — 1 byte per cell.
- All four pixel/priority functions have `(unsigned)x >= W || (unsigned)y >= H` guards.

### Flash game/save cache (littlefs) — `flashfs.c`, `FLASHFS_ENABLED`
The DVI room-transition **signal blanking was root-caused to SD being slow**: a room load
spends ~300 ms in core0 SD I/O, starving core1's software-TMDS fill loop long enough that the
monitor drops sync. **Fix: cache the game into the RP2350's spare flash and play from there**
(XIP reads are ~instant, so core1 is never starved). This was proven first with a fixed flash
archive (`DVI_EMBED_GAME`, kept as a guarded single-game test path) and then productised as a
**littlefs cache** shared by all targets.
- **Flash layout** (offset from `0x10000000`): `0x000000..0x100000` firmware (1 MB reserved),
  `0x100000..end` littlefs (`FLASHFS_SIZE = PICO_FLASH_SIZE_BYTES - 1 MB`; 15 MB on DVI, 3 MB
  on the 4 MB LCD boards). Block device: reads = XIP `memcpy`, prog/erase = `flash_range_*`.
- **Cache-management menu (`sdcard.c`)**: the chooser merges SD ∪ cached games with tags
  `[SD]` / `[flash]` / `[SD+flash]`. **ENTER** plays — from flash if cached, else straight from
  SD (`play_from_flash = choice.in_flash`; `get_file`/`read_file_at` route to `flashfs_read_*`
  when set, else SD). **`R`** caches/re-caches the selected SD game to flash and stays in the
  menu; **`D`** deletes its cache. **No auto-caching** — a game is only ever cached because you
  pressed `R`. So on **DVI**, playing an *uncached* game via ENTER reads from SD → the
  transition blanking returns; press `R` first for the smooth path. Files stored under
  `/games/<name>/`, saves under `/saves/`; caching lowercases filenames to the engine's bare
  lowercase names and **only copies AGI resources** (`logdir/picdir/viewdir/snddir/object/
  words.tok/vol.*` via `is_agi_resource()`) — not the DOS `AGI` exe / overlays / docs. SD is
  **optional** — cached games play with no card inserted.
- **Cache full / free-space UX**: the chooser header shows `Flash: <n.n>M free / <total>M` via
  `flashfs_df()` (`lfs_fs_size()` → used; total = `FLASHFS_SIZE`), computed on entry and after each
  R/D (not per-keypress). `flashfs_df()` is guarded by `mounted` + `lfs_fs_size() >= 0`, so it
  reports "full free, 0 used" and never faults if the fs is unmounted or corrupt. If a cache
  **fails** (flash full / mid-copy error) the `R` handler deletes the partial cache
  (`flashfs_delete_game`) and shows "Cache FAILED" — you never get a broken `[flash]` entry.
  A ~0.5 MB game fits ~28× in the DVI targets' 15 MB region (restouch's is 3 MB).
- **Compile-gated by `FLASHFS_ENABLED`** (in sdcard.c too): with the cache off (picocalc) the
  `R`/`D` hints are hidden, the keys are no-ops, and there is **no flash-write path at all** (no
  init/format, cache/delete compiled out, saves → SD) so the cache region is never touched.
  After a game loads, `main.c` does an `lcd_clear()` so the "Loading:" text doesn't linger in
  the TFT margins (the centered 320×200 flush never repaints them).
- **Self-heal**: `flashfs_init()` reformats if mount fails *or* if a post-mount `lfs_fs_size()`
  reports corruption (a partial fs left by an earlier interrupted/failed write mounts fine but
  fails every op — this recovers it automatically; wipes the cache).
- **Saves**: SD when the game is on SD and a card is present (`sd_available && game_on_sd`),
  else littlefs `/saves/<name>.sav` — so SD-less play can still save (separate slot from SD).
- **Flash-write vs the DVI (critical)**: flash erase/program disables XIP, so no core may
  execute/read flash during a write. `multicore_lockout` **deadlocks** against core1's real-time
  loop — do NOT use it. Instead: (a) the format-on-first-boot runs in `flashfs_init()` **before
  core1 is launched** (single-core, `flashfs_write_lock` is a no-op then); (b) caching/saves
  that happen while core1 runs use a **cooperative pause** — `flashfs_write_lock` sets a flag,
  core1 (in `display.cpp`) calls `dvi_inst->stop()`, disables IRQs, spins in a RAM-only loop
  during the write, then `dvi_inst->start()` (clean DMA restart; the monitor re-syncs). So the
  screen goes dark during caching — the chooser's `R` handler shows a "Caching… screen may go
  DARK" message *before* the pause (core1 can't draw during it). LCD targets are single-core so
  `flashfs_write_lock` is the weak no-op there; only per-op `save_and_disable_interrupts` (in the
  block device) is needed.
- **littlefs geometry**: `block_size = 4096` (flash sector), `prog/read_size = 256`, static
  read/prog/lookahead + per-file caches (one lfs file open at a time; game reads and the save
  file use separate cache buffers).
- SD baud on DVI is overclocked to **40 MHz** (`dvi/hw_config.c`, effective ~31.5 MHz) to speed
  up the caching copy — card-dependent; drop to 12.5/25 MHz if a card errors.
- **Shared-bus baud (restouch)**: LCD + SD share spi1, and each device only sets its own baud
  around its ops — so they'd clobber each other. Two reclaims fix it: `lcdspi_set_address()`
  reclaims `LCD_SPI_CLOCK_HZ` (80 MHz) per draw, and `sd_reclaim_bus()` (`sd_read_*`/`sd_save_open`/
  chooser scan/`flashfs_cache_game`) reclaims the SD baud before every SD access. Harmless on
  picocalc (SD on spi0, LCD on spi1 — separate) and DVI (SD on spi1, display is PIO).

### Game directory selection
`show_dir_chooser()` in sdcard.c lists games from SD (`/agi/*`) **and** the flash cache, merged
and tagged; ENTER fills a `game_choice_t` (name + on_sd/in_flash/refresh). See "Flash game/save
cache". With `FLASHFS_ENABLED=0` it degrades to SD-only (flashfs unmounted → lists SD only).

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

### parse() resets FLAG_4 and clears the command line (get.string search fix)
`parse(str)` (`commands/string.c`) re-parses a string var as if freshly typed — used by the
`get.string` search dialogues (SQ1 data-archive "astral body" search; also the name-entry screen).
Two fixes make those work:
- **Reset `FLAG_4_SAID_ACCEPTED_INPUT` after re-parsing.** `said()` (`commands/test.c`) has an
  "already accepted this cycle" latch: once a `said()` matches it sets `FLAG_4`, and later `said()`s
  that cycle return false. The archive flow is `said("search")` → `get.string` → `parse()` →
  `said("astral body")` all in **one** cycle, so the opening `said("search")` had already set
  `FLAG_4` → the post-parse `said()` was ignored and you had to type the search **twice**. `parse()`
  now clears `FLAG_4` so the re-parsed input matches first try.
- **Clear `input_buffer`/`input_pos` after parsing.** `parse()` fills them only as scratch for the
  word-group parser, but the text came from a string var (get.string), not the command line — left
  set, it **lingered on the command prompt** after the dialogue closed (e.g. "astral body", or the
  entered name after the intro). Cleared now.

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

### DVI: room-transition hang — FIXED (DMA-poll SD driver)
Was a cross-core lost-wakeup in the SD `sem_acquire`/`__wfe` DMA wait under DVI-DMA contention.
Fixed by `dvi/sd_spi_poll.c` (patched-copy SD driver that polls `dma_channel_is_busy` instead of
the semaphore) — see "DVI: room-transition hang" in Key design decisions. The poll driver is
**required** (on-device A/B: much worse without it); the keep-alive is kept as a secondary belt.

### DVI: room-transition *signal blanking* — FIXED (play from flash cache)
During a room load the DVI signal used to drop (monitor dark a few seconds, sometimes never
recovering). **Root cause: SD is slow** — a load spends ~300 ms in core0 SD I/O, which starves
core1's software-TMDS fill loop long enough that the monitor loses sync. (The fill loop is a CPU
loop and can't be given bus priority over another CPU, which is why buffers / bus-priority / the
dir-cache — which only cut file *opens*, not the data-transfer time — didn't help.) **Fixed by the
flash game/save cache**: games are copied to littlefs in flash and played from there at XIP speed,
so core0's per-transition work drops from ~300 ms to ~10–30 ms and core1 is never starved.
Confirmed on device: transitions are fast (just a brief red flash), no blanking. See "Flash
game/save cache (littlefs)" in Key design decisions. (HSTX would also fix it by removing the core1
fill loop entirely, but is unnecessary now — kept as a note if a software-TMDS-free path is ever
wanted.) Fallback if the cache misbehaves: `FLASHFS_ENABLED=0` reverts to SD-direct (blanking
returns).

### DVI: HDMI audio glitches audio-decoding monitors (audio ON by default)
`DVI_HDMI_AUDIO=1`/`SOUND_ENABLED=1` (now default) play sound via HDMI data-islands, but can glitch
the DVI signal on **monitors that decode HDMI audio** (black, re-sync ~10 s); a monitor with no audio
is fine with the same build. Ruled out power (two good supplies). The DMA bus-priority that once
masked the non-audio case has since been removed (it didn't help overall). Remaining suspect: the
**audio data-islands** (CTS-N / audio InfoFrame / pico_lib data-island spec compliance) which an
audio sink rejects — see the fix hints under "DVI: HDMI data-island audio". Fallback for a
mis-behaving audio monitor: build with `SOUND_ENABLED=0 DVI_HDMI_AUDIO=0`.

### RP2350-PiZero: no 5 V on HDMI pin 18 / USB — TV + USB-host both fail (hardware; SETTLED)
The Waveshare **RP2350**-PiZero does **not** route VBUS/5 V to its connectors. Two symptoms, one
root cause:
- **HDMI pin 18 (+5 V source-present)** is absent → many **TVs show nothing** (they gate on that
  rail to detect a live source; PC monitors are usually more lenient).
- The **USB port isn't 5 V-powered** → it **can't power a USB keyboard** as a host.

**Signaling mode is NOT the cause** (ruled out): pico_lib supports full HDMI mode with the AVI
InfoFrame (`enableDataIsland()` / `setAudioFreq()` in `pico_lib/dvi/dvi.cpp`), and **msx2pico already
runs in that mode on this board** — the TV still stays dark. So DVI-vs-HDMI (`useDVIModeForHDMI`-style)
toggling can't fix it; no software change conjures a missing power rail.

**Fix = hardware**: inject 5 V — VSYS → 100 Ω → HDMI pin 18 (and likewise to USB VBUS for a
keyboard). The mini-HDMI connector is too small to solder easily. **The practical answer is the
RP2040-PiZero**, which *does* route 5 V → its DVI works on regular TVs and its native-USB host powers
a keyboard (this is why `tinyagi_dvi_rp2040` exists — see the RP2040 build target). Don't re-investigate
this as a signal/mode problem.

### DVI: horizontal — left 320 of the line (works well in practice, low priority)
The 320-pixel AGI frame is written 1:1 into the left 320 of the 640-wide DVI line, right half
black (`display.cpp` core1 loop + `memset(dst + 320, …)`). **In practice this displays fine** —
tested TVs scale the picture to fill the screen, so it looks full-screen and correct; not worth
changing. If a true 640-wide fill were ever wanted, horizontal 2× pixel-doubling would do it, but
naive doubling makes the 8px chooser font "fat" — it'd need a mode flag that doubles the game but
renders text 1:1 (or a 640-wide text path). **Low priority / effectively a non-issue.**

### DVI: double-buffer — DONE
Implemented (`DVI_DOUBLE_BUFFER=1`, default): core1 scans a `scanout_buffer` that `flush_display()`
copies the composed framebuffer into once per cycle. Removes the black-during-load flash and tearing.
See "DVI: display model" under Key design decisions. (Does not fix the transition *signal blanking*.)

### flashfs: potential improvements (not done)
- **Keep the screen up during caching** (best UX win): instead of `dvi_inst->stop()` during the
  copy, keep core1 running so it shows a static "Caching…" frame the whole time. Needs *everything*
  core1 touches to be in RAM — its code already is (`__not_in_flash_func`), but the palette
  (`agi_palette555`, currently flash rodata) must move to RAM and pico_lib's IRQ path must be
  confirmed flash-free. Medium effort.
- **Force-format / recovery (mostly done)**: `flashfs_init()` now auto-reformats both a
  bad-superblock *and* a "mounts-but-corrupt" fs (post-mount `lfs_fs_size()` check), which covers
  the partial-corruption case. A user-triggered wipe (boot key-combo → "Reformat? Y/N", or a menu
  key) would still be a nice-to-have. Manual fallback: `picotool erase --range <fs-start> <end>`.
- **Atomic refresh**: `R` re-cache currently overwrites files in place (`LFS_O_TRUNC`); switch to
  temp-dir + `lfs_rename` so a power loss mid-refresh can't corrupt the existing cached game.
- **SD-SPI overclock beyond 40 MHz**: `dvi/hw_config.c` `.baud_rate` — only affects caching speed
  now (gameplay is from flash); card-dependent, A/B before raising further.

### Available (not applied): dir-cache "faster loads" patch
`tinyagi-rp2350/patches/dircache_faster_loads.patch` — an engine-level speedup (caches the 4 dir
files in RAM + reads each resource's size-header and data in one `f_open` instead of two), cutting
per-transition file-*opens* ~3×. Correctness-preserving, benefits all targets. It was written while
chasing the signal blanking and did **not** fix it — **not** because SD was innocent (it was the
cause; the flash cache proved that) but because it only cut file *opens*, not the resource
*data-transfer* time that dominates a load. Superseded on DVI by the flash cache; still a valid
standalone "faster loads" speedup for SD-direct play (e.g. picocalc) — `git apply` from repo root.

### RESTOUCH: shared spi1 LCD/SD baud — FIXED
LCD and SD share spi1, and neither re-asserted its own baud per op, so whoever used the bus last
set its clock: after an SD access the LCD ran at the SD's 12.5 MHz (slow), and with **no SD
inserted** the failed SD init left the bus at ~100–400 kHz, so the TFT crawled at seconds-per-frame
(flashfs's SD-less mode exposed this). Fixed by two reclaims: `lcdspi_set_address()` sets
`LCD_SPI_CLOCK_HZ` (80 MHz) per draw, and `sd_reclaim_bus()` sets the SD baud before every SD
access (see "Shared-bus baud" under Flash game/save cache). LCD is now always 80 MHz and SD always
12.5 MHz, regardless of order. (Bonus: LCD is faster than the old 12.5-MHz-after-SD behaviour too.)

### Display overlay buffer (not yet implemented — plan documented)
**Problem.** `display()` (`commands/display.c:46`) draws text via `_draw_text` → `_draw_char`
(`text_display.c`) → `screen_set_320` into the framebuffer. The per-cycle order in `interpreter.c`
is `agi_draw_all_active()` (sprites, ~:663) → `execute_logic_cycle()` (~:665, which only *repaints*
`display()` text on a keypress). So on **idle** cycles the logic doesn't repaint the text and the
sprite is drawn over it → e.g. PQ1's character shows as a silhouette over the newspaper (PQ1 uses
`clear_text_rect()` + `display()` each cycle, but only when a key is pressed).

**Planned fix — a cell-based overlay that re-composites `display()` text over sprites** (engine
code, benefits all targets; ~50–60 lines):
- **State** in `text_display.c`: `char_overlay[25][40]` + `attr_overlay[25][40]` (fg<<4 | bg, both
  0–15) = **~2 KB packed** (vs ~3 KB with separate fg/bg arrays). Plus an `overlay_mode` flag
  (off / record / clear). Note: it's `.bss`, so on the RAM-tight RP2040 it comes out of the ~77 KB
  heap (→ ~75 KB) — consider a compile flag if that bites, since PQ1 is the RAM-heavy case.
- **Hooks** (all funnel through `_draw_char`, the single text choke point):
  1. `display()` wraps its `_draw_text` in `overlay_mode = record` → each drawn cell captured at
     `row = start_y/8, col = start_x/8` (bounds-check <25 / <40).
  2. `clear_text_rect()` / `clear_lines()` wrap in `overlay_mode = clear` → cleared cells removed.
  3. New `apply_display_overlay()` redraws every non-empty overlay cell via `_draw_char` (mode off,
     so it doesn't re-record). Called in `interpreter.c` **after** `execute_logic_cycle()`, guarded
     by `!agi_text_mode` (matches the sprite-draw guard) → text always ends up on top.
  4. Clear the whole overlay on `graphics()` (`display.c:59`), `text_screen()` (`:156`), and
     `new_room()` (`control_flow.c:53`).
- `_draw_char` already tags text pixels with priority 255, so re-applying each cycle is idempotent.
- **Correctness risk to watch:** the overlay persists text until *explicitly* cleared. PQ1 and the
  tested games are well-behaved (they clear via `clear_text_rect`/`clear_lines`/room change). A game
  that paints a **picture over display-text without calling a clear** would get stale text redrawn
  over it — narrow, but worth checking when broadening game testing.

Note: distinct from the `agi_text_mode` path — games that call `text_screen()` (e.g. SQ1 library)
suppress sprite drawing entirely and don't need the overlay.

### Sound on/off toggle (FLAG_9) — FIXED via output mute
`FLAG_9_SOUND_ENABLED` (default true — `state.c:92`; toggled by the game's menu / F2) now actually
silences audio. `platform_tick_sound()` calls `pwm_synth_set_muted(!FLAG_9)` every cycle, and both
output paths — the PWM ISR (`pih`, picocalc) and the HDMI render (`pwm_synth_render`, RP2350 DVI) —
emit silence when muted. It mutes only the **output**: the sound *sequencer* keeps running
(`agi_sound_tick`), so sound-paced logic stays correctly timed and the sound-done flag still fires at
the sound's natural end (consistent with "Sound timing decoupled from audio output"). So `sound()` /
`stop_sound()` are unchanged — no "complete immediately" hack, no timing regression, no hang. Harmless
no-op on `SOUND_ENABLED=0` targets (restouch/RP2040), which have no audio output anyway.

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
- **Space Quest 1** — plays through correctly; menu, F1, quit, save/restore all work. Library / data-archive computer (text_screen mode) works, incl. the "astral body" `get.string` search — matches first try and leaves no leftover text on the prompt (see "parse() resets FLAG_4…"). Typing "exit" at the terminal falls through silently (correct — word group 157 is not handled by the library logic; type an unrecognised word or press ESC to close the terminal cleanly).
- **Space Quest 2** — plays through correctly including intro sequence.
- **Police Quest 1** — partially tested. Newspaper room (Logic 116) renders correctly with the rendering-order fix; character sprite appears as a dark silhouette on idle frames (see display overlay buffer TODO). Exit via "close paper", "put down paper", or "stop reading".

Per-target notes: PicoCalc and RESTOUCH play the above. The **DVI** target boots, runs the
chooser, and plays SQ1 (walking, room transitions, save/restore) with `DVI_KEEPALIVE_TIMER=1`;
outstanding DVI items are the three known issues above (root-cause of the hang, full-screen
horizontal scaling, double-buffering).
