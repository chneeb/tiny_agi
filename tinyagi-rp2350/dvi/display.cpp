/* DVI display driver for RP2350-PiZero (Waveshare) via pico_lib DVI.
 *
 * Board pin assignment (from msx2pico iopins.h, confirmed working):
 *   DVI TMDS: Blue=GPIO36, Green=GPIO34, Red=GPIO32, Clock=GPIO38
 *   Requires PICO_PIO_USE_GPIO_BASE=1 (GPIOs 32-39 are above normal PIO range).
 *
 * Output: 640x480 @ 60 Hz.  pico_lib N_LINE_PER_DATA=2 line-doubles our 240
 * unique scanlines to 480 DVI scanlines automatically (vertical).  Horizontally
 * the 320-pixel AGI frame is written 1:1 into the left 320 entries of the
 * 640-wide LineBuffer (right half black).  Full-screen horizontal scaling is
 * a TODO — see notes in this file's history; it must not distort the 8px
 * chooser font.  AGI 320x200 is centred with 20-line black borders (V_MARGIN).
 *
 * Core1 owns the DVI scanout loop; core0 writes the framebuffer freely.
 * flush_display() is a no-op — DVI reads the framebuffer on every scan.
 *
 * lcd_clear() and lcd_print_string() render the 8x8 CP437 font into the
 * framebuffer (40 cols x 25 rows).  Used only for the pre-game dir chooser.
 *
 * DEBUG: dvi_debug_get_loop_frames() / dvi_debug_get_scanout_frames() expose
 * two liveness counters so the game loop can report whether core1's fill loop
 * and the DMA scanout IRQ are still advancing (used to diagnose signal loss
 * once a game starts).
 */

#include "display.h"
#include "dvi/dvi.h"
#include "dvi/timing.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/sync.h"   // save_and_disable_interrupts (flash-write pause)
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* AGI EGA 16-colour palette in RGB555 (bits [14:10]=R, [9:5]=G, [4:0]=B). */
static const uint16_t agi_palette555[16] = {
    0x0000,  /*  0 Black        #000000 */
    0x0015,  /*  1 Dark Blue    #0000AA */
    0x02A0,  /*  2 Dark Green   #00AA00 */
    0x02B5,  /*  3 Dark Cyan    #00AAAA */
    0x5400,  /*  4 Dark Red     #AA0000 */
    0x5415,  /*  5 Dark Magenta #AA00AA */
    0x5540,  /*  6 Brown        #AA5500 */
    0x56B5,  /*  7 Light Gray   #AAAAAA */
    0x294A,  /*  8 Dark Gray    #555555 */
    0x295F,  /*  9 Light Blue   #5555FF */
    0x2BEA,  /* 10 Light Green  #55FF55 */
    0x2BFF,  /* 11 Light Cyan   #55FFFF */
    0x7D4A,  /* 12 Light Red    #FF5555 */
    0x7D5F,  /* 13 Light Magenta #FF55FF */
    0x7FEA,  /* 14 Yellow       #FFFF55 */
    0x7FFF,  /* 15 White        #FFFFFF */
};

static uint8_t priority_buffer[160 * 168];

/* DVI_DOUBLE_BUFFER: core1 scans a dedicated front buffer instead of the live
 * framebuffer; flush_display() copies the composed framebuffer into it once per
 * game cycle.  This holds the previous frame through a room load and removes
 * tearing.  Costs +64 KB SRAM at 8-bit.  RP2350 uses this.
 *
 * DVI_PACKED_FB (default on for RP2040): store 2 pixels per byte (4-bit — AGI is
 * 16 colours, lossless).  The framebuffer AND a scanout buffer then fit in 32 KB
 * each = 64 KB total — the same RAM as a single 8-bit framebuffer — so RP2040
 * gets double-buffering (no flicker) at NO extra RAM.  Core1 unpacks two pixels
 * per byte (half the SRAM reads of 8-bit, so ~neutral on its tight timing);
 * screen_set_320 / lcd_putchar do a nibble read-modify-write. */
#ifndef DVI_DOUBLE_BUFFER
#define DVI_DOUBLE_BUFFER 1
#endif
#ifndef DVI_PACKED_FB
#if RP2040_PIZERO
#define DVI_PACKED_FB 1
#else
#define DVI_PACKED_FB 0
#endif
#endif

#if DVI_PACKED_FB
#define FB_BYTES  (320 * 200 / 2)   /* 2 px/byte */
#define FB_STRIDE 160               /* bytes per row */
#else
#define FB_BYTES  (320 * 200)
#define FB_STRIDE 320
#endif
/* A scanout buffer (flush copies into it) exists for both the 8-bit and packed
   double buffers; live single-buffer scans the framebuffer directly. */
#define DVI_HAS_SCANOUT (DVI_DOUBLE_BUFFER || DVI_PACKED_FB)

static uint8_t framebuffer[FB_BYTES];
#if DVI_HAS_SCANOUT
static uint8_t scanout_buffer[FB_BYTES];
#define SCAN_BUF scanout_buffer
#else
#define SCAN_BUF framebuffer
#endif

#if DVI_PACKED_FB
/* Set pixel (x,y) to 4-bit colour c in a packed buffer; x&1 selects the nibble. */
static inline void fb_put(uint8_t *buf, int x, int y, uint8_t c) {
    uint8_t *b = &buf[y * FB_STRIDE + (x >> 1)];
    if (x & 1) *b = (uint8_t)((*b & 0x0F) | (c << 4));
    else       *b = (uint8_t)((*b & 0xF0) | (c & 0x0F));
}
#endif

#define DVI_H       240
#define DVI_W       640
#define V_MARGIN    20             /* (240 - 200) / 2 */

static dvi::DVI *dvi_inst;

/* Liveness counters for diagnosing DVI signal loss (see header comment).
 * volatile: written on core1, read on core0 without a lock. */
static volatile uint32_t dvi_loop_frames = 0;

/* Dedicated core1 stack (8 KB) in main RAM — replaces the default ~3 KB
 * SCRATCH_Y stack, both to test the stack-overflow theory and to move the
 * stack away from whatever core0 activity may be corrupting it. */
#if RP2040_PIZERO
static uint32_t core1_stack[1024] __attribute__((aligned(8)));   /* 4 KB — DVI loop is shallow; saves heap on RP2040 */
#else
static uint32_t core1_stack[2048] __attribute__((aligned(8)));
#endif

/* ── core1 fault capture ─────────────────────────────────────────────────
 * Overrides the SDK's weak isr_hardfault (shared vector table).  Whichever
 * core faults lands here; we record the stacked PC/LR + fault-status regs
 * and park.  core0 prints g_fault so we learn exactly where core1 died. */
extern "C" {
    struct fault_info_t {
        volatile uint32_t valid;
        uint32_t cpuid, pc, lr, psr, cfsr, hfsr, bfar, mmfar, sp, r0, r3, r12;
        uint32_t stk[8];   /* words at/above the exception frame */
    };
    static fault_info_t g_fault;

    __attribute__((used)) void hardfault_report(uint32_t *frame) {
        g_fault.cpuid = *(volatile uint32_t *)0xd0000000; /* SIO CPUID */
        g_fault.r0    = frame[0];
        g_fault.r3    = frame[3];
        g_fault.r12   = frame[4];
        g_fault.lr    = frame[5];
        g_fault.pc    = frame[6];
        g_fault.psr   = frame[7];
        g_fault.cfsr  = *(volatile uint32_t *)0xE000ED28;
        g_fault.hfsr  = *(volatile uint32_t *)0xE000ED2C;
        g_fault.mmfar = *(volatile uint32_t *)0xE000ED34;
        g_fault.bfar  = *(volatile uint32_t *)0xE000ED38;
        g_fault.sp    = (uint32_t)frame;
        for (int i = 0; i < 8; i++) g_fault.stk[i] = frame[i + 8];
        g_fault.valid = 1;
        /* Print directly from the fault handler: on a core0 fault the main loop
         * is dead, so the deferred dvi_debug_print_fault() would never run and
         * the freeze would look completely silent (no watchdog either, since
         * this handler outranks all IRQs).  stdio here is best-effort. */
        printf("\n*** HARDFAULT core%lu pc=%08lx lr=%08lx psr=%08lx "
               "cfsr=%08lx hfsr=%08lx bfar=%08lx sp=%08lx r0=%08lx r3=%08lx r12=%08lx ***\n",
               (unsigned long)g_fault.cpuid, (unsigned long)g_fault.pc,
               (unsigned long)g_fault.lr,   (unsigned long)g_fault.psr,
               (unsigned long)g_fault.cfsr, (unsigned long)g_fault.hfsr,
               (unsigned long)g_fault.bfar, (unsigned long)g_fault.sp,
               (unsigned long)g_fault.r0,   (unsigned long)g_fault.r3,
               (unsigned long)g_fault.r12);
        while (true) tight_loop_contents();
    }

    __attribute__((naked)) void isr_hardfault(void) {
        __asm volatile(
            "mrs  r0, msp          \n" /* default: main stack */
            "mov  r1, lr           \n"
            "movs r2, #4           \n" /* EXC_RETURN bit 2 => PSP was used */
            "tst  r1, r2           \n"
            "beq  1f               \n"
            "mrs  r0, psp          \n"
            "1:                    \n"
            "b    hardfault_report \n"
        );
    }

    void dvi_debug_print_fault(void) {
        if (!g_fault.valid) return;
        printf("FAULT core%lu: pc=%08lx lr=%08lx psr=%08lx cfsr=%08lx "
               "hfsr=%08lx bfar=%08lx mmfar=%08lx sp=%08lx "
               "r0=%08lx r3=%08lx r12=%08lx\n",
               (unsigned long)g_fault.cpuid, (unsigned long)g_fault.pc,
               (unsigned long)g_fault.lr,  (unsigned long)g_fault.psr,
               (unsigned long)g_fault.cfsr, (unsigned long)g_fault.hfsr,
               (unsigned long)g_fault.bfar, (unsigned long)g_fault.mmfar,
               (unsigned long)g_fault.sp,
               (unsigned long)g_fault.r0,  (unsigned long)g_fault.r3,
               (unsigned long)g_fault.r12);
        printf("  stack@frame:");
        for (int i = 0; i < 8; i++)
            printf(" %08lx", (unsigned long)g_fault.stk[i]);
        printf("\n");
    }
}

static const dvi::Config dvi_cfg = {
#if RP2040_PIZERO
    .pinTMDS  = {26, 24, 22},  /* Blue, Green, Red — Waveshare RP2040-PiZero mini-HDMI */
    .pinClock = 28,
#else
    .pinTMDS  = {36, 34, 32},  /* Blue, Green, Red — Waveshare RP2350-PiZero (GPIO 32-39) */
    .pinClock = 38,
#endif
    .invert   = false,
};

// Cooperative pause so core0 can write flash (erase/program disables XIP) without
// core1 executing/reading flash. Set true once core1's scanout is up.
static volatile bool core1_lockout_ready = false;
static volatile bool dvi_pause_req = false;   // core0 -> core1: park yourself
static volatile bool dvi_paused    = false;   // core1 -> core0: parked, safe to write

static void __not_in_flash_func(core1_dvi_loop)(void) {
    dvi_inst->registerIRQThisCore();
    dvi_inst->start();
    core1_lockout_ready = true;
    while (true) {
        if (dvi_pause_req) {
            dvi_inst->stop();                       // clean DMA/serialiser halt (XIP still on here)
            uint32_t ints = save_and_disable_interrupts();
            dvi_paused = true;
            while (dvi_pause_req) tight_loop_contents();  // RAM-only spin during the flash write
            dvi_paused = false;
            restore_interrupts(ints);
            dvi_inst->start();                      // clean restart; monitor re-syncs
        }
        for (int y = 0; y < DVI_H; y++) {
            dvi::DVI::LineBuffer *lb = dvi_inst->getLineBuffer();
            uint16_t *dst = lb->data();
            int agi_y = y - V_MARGIN;
            if (agi_y >= 0 && agi_y < 200) {
                const uint8_t *src = &SCAN_BUF[agi_y * FB_STRIDE];
#if DVI_PACKED_FB
                for (int x = 0; x < 320; x += 2) {
                    uint8_t b = src[x >> 1];
                    dst[x]     = agi_palette555[b & 0x0F];
                    dst[x + 1] = agi_palette555[b >> 4];
                }
#else
                for (int x = 0; x < 320; x++)
                    dst[x] = agi_palette555[src[x] & 0x0F];
#endif
                memset(dst + 320, 0, (DVI_W - 320) * sizeof(uint16_t));
            } else {
                memset(dst, 0, DVI_W * sizeof(uint16_t));
            }
            dvi_inst->setLineBuffer(y, lb);
            dvi_inst->convertScanBuffer15bpp();
        }
        dvi_loop_frames++;   /* one full frame fed to the encoder */
    }
}

// flashfs flash-write critical section (overrides the weak no-ops in flashfs.c):
// pause core1's scanout so it isn't executing/reading flash while XIP is disabled
// for an erase/program. The DVI signal drops for the duration (fine for the rare,
// one-time caching / save writes). Waits until core1 has registered as a victim.
extern "C" void flashfs_write_lock(void) {
    if (!core1_lockout_ready) return;             // core1 not started -> single-core, no pause
    dvi_pause_req = true;
    while (!dvi_paused) tight_loop_contents();    // wait for core1 to park (<= ~1 frame)
}
extern "C" void flashfs_write_unlock(void) {
    if (!core1_lockout_ready) return;
    dvi_pause_req = false;
    while (dvi_paused) tight_loop_contents();      // wait for core1 to resume
}

/* Core0-callable liveness probes.
 *   loop counter   advancing => core1 fill loop is running & not blocked
 *                              in getLineBuffer() (free queue not drained).
 *   scanout counter advancing => the DMA completion IRQ is firing, i.e. TMDS
 *                              is really being clocked out to the DVI pins.
 * If the screen goes dark: both frozen => core1 stalled/crashed; scanout
 * frozen but loop advancing (transient) => DMA/IRQ died; both advancing =>
 * signal is live and the fault is downstream (content/cable/monitor). */
extern "C" uint32_t dvi_debug_get_loop_frames(void) { return dvi_loop_frames; }
extern "C" uint32_t dvi_debug_get_scanout_frames(void) {
    return dvi_inst ? dvi_inst->getFrameCounter() : 0;
}

/* Dump the DMA hardware state so core0 can see why DMA_IRQ_0 (DVI) stopped.
 *   ints0 stuck non-zero  => core1's IRQ handler is NOT running (crashed /
 *                            stack overflow) — pending IRQs never cleared.
 *   inte0 lost DVI bits   => something cleared the IRQ0 enable mask.
 *   a channel with ahb/rd/wr err => a DMA bus error broke the DVI chain.
 *   all clean but idle     => the DMA chain completed and never re-armed. */
extern "C" void dvi_debug_dump_dma(void) {
    printf("DMA dbg: loop=%lu scan=%lu inte0=%08lx ints0=%08lx "
           "inte1=%08lx ints1=%08lx\n",
           (unsigned long)dvi_loop_frames,
           (unsigned long)(dvi_inst ? dvi_inst->getFrameCounter() : 0),
           (unsigned long)dma_hw->inte0, (unsigned long)dma_hw->ints0,
           (unsigned long)dma_hw->inte1, (unsigned long)dma_hw->ints1);
    for (int i = 0; i < (int)NUM_DMA_CHANNELS; i++) {
        uint32_t ctrl = dma_channel_hw_addr(i)->ctrl_trig;
        bool busy   = ctrl & DMA_CH0_CTRL_TRIG_BUSY_BITS;
        bool ahberr = ctrl & DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS;
        bool rderr  = ctrl & DMA_CH0_CTRL_TRIG_READ_ERROR_BITS;
        bool wrerr  = ctrl & DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
        bool en0    = dma_hw->inte0 & (1u << i);
        bool en1    = dma_hw->inte1 & (1u << i);
        /* Only print channels that are in use or faulted, to limit noise. */
        if (busy || ahberr || rderr || wrerr || en0 || en1)
            printf("  ch%02d ctrl=%08lx busy=%d irq0en=%d irq1en=%d "
                   "ahberr=%d rderr=%d wrerr=%d tc=%lu\n",
                   i, (unsigned long)ctrl, busy, en0, en1,
                   ahberr, rderr, wrerr,
                   (unsigned long)dma_channel_hw_addr(i)->transfer_count);
    }
}

/* Text cursor for lcd_clear / lcd_print_string (40-col x 25-row grid). */
static int text_col = 0;
static int text_row = 0;

extern "C" uint8_t font_data[2048];  /* defined in platform.c; 256 glyphs x 8 rows */

/* display.h declares these as extern "C" via its __cplusplus guards. */

void display_init(void) {
    memset(framebuffer,     0, sizeof(framebuffer));
    memset(priority_buffer, 0, sizeof(priority_buffer));
#if DVI_HAS_SCANOUT
    memset(scanout_buffer,  0, sizeof(scanout_buffer));  /* no garbage before first flush */
#endif
    dvi_inst = new dvi::DVI(pio0, &dvi_cfg, dvi::getTiming640x480p60Hz());
    multicore_launch_core1_with_stack(core1_dvi_loop, core1_stack,
                                      sizeof(core1_stack));
}

#if DVI_HDMI_AUDIO
/* HDMI audio: AGI sound is carried as data-island audio in the DVI stream, so
 * it plays through the monitor's speakers (needs an HDMI sink that decodes
 * audio — a pure DVI display stays silent).  core0 pushes mono int16 samples
 * (duplicated to stereo) into pico_lib's SPSC ring; core1's per-line
 * updateDataPacket() drains it.  44100 Hz, N=6144, CTS auto from pixel clock. */
/* DVI_AUDIO_DATAISLANDS=0 isolation build: set up the ring + producer but do NOT
 * call setAudioFreq(), so data islands stay disabled and core1 does zero extra
 * per-scanline work (and there is no sound).  If the black screen goes away with
 * this, the culprit is the data-island load on core1; if it persists, it's the
 * core0 producer/sound path or power. */
#ifndef DVI_AUDIO_DATAISLANDS
#define DVI_AUDIO_DATAISLANDS 1
#endif

extern "C" void dvi_audio_init(void) {
#if DVI_AUDIO_DATAISLANDS
    dvi_inst->setAudioFreq(44100, 0, 6144);   /* also enables data islands */
#endif
    dvi_inst->allocateAudioBuffer(1024);       /* power-of-two ring, ~4 KB */
    dvi_inst->getAudioRingBuffer().advanceWritePointer(255);  /* prime silence */
}

extern "C" uint32_t dvi_audio_writable(void) {
    return dvi_inst ? dvi_inst->getAudioRingBuffer().getWritableSize() : 0;
}

/* Write up to n mono samples (duplicated L/R).  getWritableSize() is a
 * contiguous run, so a straight copy is safe without wrap handling. */
extern "C" void dvi_audio_write(const int16_t *mono, int n) {
    if (!dvi_inst || n <= 0) return;
    auto &ring = dvi_inst->getAudioRingBuffer();
    uint32_t avail = ring.getWritableSize();
    if ((uint32_t)n > avail) n = (int)avail;
    dvi::DVI::AudioSample *dst = ring.getWritePointer();
    for (int i = 0; i < n; i++) { dst[i][0] = mono[i]; dst[i][1] = mono[i]; }
    ring.advanceWritePointer((uint32_t)n);
}
#endif /* DVI_HDMI_AUDIO */

void flush_display(void) {
#if DVI_HAS_SCANOUT
    /* Publish the composed frame to the buffer core1 scans, so only completed
       frames are shown (no tearing/flicker mid-cycle). Called once per cycle.
       32 KB when packed, 64 KB at 8-bit. */
    memcpy(scanout_buffer, framebuffer, sizeof(framebuffer));
#else
    /* Single-buffer: no-op — core1 scans the live framebuffer directly. */
#endif
}

void screen_set_160(int x, int y, int color) {
    if ((unsigned)x >= 160 || (unsigned)y >= 200) return;
#if DVI_PACKED_FB
    /* The two adjacent 320-px pixels are the two nibbles of one packed byte. */
    framebuffer[y * FB_STRIDE + x] = (uint8_t)(((color & 0x0F) << 4) | (color & 0x0F));
#else
    framebuffer[y * 320 + x * 2]     = (uint8_t)color;
    framebuffer[y * 320 + x * 2 + 1] = (uint8_t)color;
#endif
}

void screen_set_320(int x, int y, int color) {
    if ((unsigned)x >= 320 || (unsigned)y >= 200) return;
#if DVI_PACKED_FB
    fb_put(framebuffer, x, y, (uint8_t)color);
#else
    framebuffer[y * 320 + x] = (uint8_t)color;
#endif
}

int priority_get(int x, int y) {
    if ((unsigned)x >= 160 || (unsigned)y >= 168) return 0;
    return priority_buffer[y * 160 + x];
}

void priority_set(int x, int y, int priority) {
    if ((unsigned)x >= 160 || (unsigned)y >= 168) return;
    priority_buffer[y * 160 + x] = (uint8_t)priority;
}

void agi_shake_screen(uint8_t times) {
    /* DVI scanout is continuous; a per-frame V-offset shake is not
       straightforward without vsync coupling.  Stub for now. */
    (void)times;
}

static void lcd_putchar(char c) {  /* internal helper — not part of public API */
    if (c == '\n') { text_col = 0; text_row++; return; }
    if (text_col >= 40) { text_col = 0; text_row++; }
    if (text_row >= 25) return;
    const uint8_t *glyph = &font_data[(uint8_t)c * 8];
    int base_x = text_col * 8;
    int base_y = text_row * 8;
    for (int row = 0; row < 8; row++) {
        int fy = base_y + row;
        if (fy >= 200) break;
        uint8_t bits = glyph[row];
        /* Draw straight to the scanned buffer — the pre-game chooser/Loading UI
           has no compose+flush cycle behind it. */
#if DVI_PACKED_FB
        for (int col = 0; col < 8; col++)
            fb_put(SCAN_BUF, base_x + col, fy, (bits & (0x80u >> col)) ? 15 : 0);
#else
        uint8_t *line = &SCAN_BUF[fy * 320 + base_x];
        for (int col = 0; col < 8; col++)
            line[col] = (bits & (0x80u >> col)) ? 15 : 0;
#endif
    }
    text_col++;
}

extern "C" void lcd_clear(void) {
    memset(SCAN_BUF, 0, sizeof(framebuffer));
    text_col = 0;
    text_row = 0;
}

extern "C" void lcd_print_string(const char *s) {
    for (; *s; s++)
        lcd_putchar(*s);
}
