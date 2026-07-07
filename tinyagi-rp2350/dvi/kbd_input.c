/* USB HID keyboard driver for the DVI target.
 *
 * RP2350-PiZero: PIO-USB host on rhport 1 (pio0 is DVI, so PIO-USB uses pio1;
 *   DVI claims DMA 0-5 so PIO-USB uses channel 7). The native USB controller is
 *   a CDC device (console). tuh_init(1); do NOT use tusb_init()/board_init().
 * RP2040-PiZero (RP2040_PIZERO): the native USB controller is the HID host on
 *   rhport 0 (tuh_init(0)) — GPIO 28 is the DVI clock there, so PIO-USB can't be
 *   used. No CDC device (console is UART). No board_init() (it resets the clock).
 * Either way tuh_task() is pumped from kbd_read(), and the HID translation +
 * mount/report callbacks below are shared (same TinyUSB host API).
 *
 * HID keycodes are translated to the virtual-key values expected by
 * platform.c's push_kbd_event():
 *   Arrows:  UP=0xB5  DOWN=0xB6  LEFT=0xB4  RIGHT=0xB7
 *   F1-F9:   0x81-0x89    F10: KB_F10_CODE
 *   ESC: 0x1B   Backspace: 0x08   Enter: KB_ENTER_CODE
 *   Printable ASCII passed through with shift applied.
 */

#include "kbd_input.h"
#include "tusb.h"
#if !RP2040_PIZERO
#include "pio_usb.h"
#endif
#include "pico/stdlib.h"
#include "pico/stdio/driver.h" /* full stdio_driver_t definition + stdio_set_driver_enabled */
#include "spi.h"          /* set_spi_dma_irq_channel() — FatFs_SPI sd_driver */
#include <string.h>
#include <stdint.h>

/* KB_ENTER_CODE and KB_F10_CODE come from CMakeLists compile definitions. */

#define KEY_BUF_SIZE 16
static int      key_buf[KEY_BUF_SIZE];
static volatile int key_head = 0;
static volatile int key_tail = 0;

static void key_push(int k) {
    int next = (key_head + 1) % KEY_BUF_SIZE;
    if (next != key_tail) {
        key_buf[key_head] = k;
        key_head = next;
    }
}

/* ── USB CDC stdio driver ──────────────────────────────────────────────────── *
 * pico_stdio_usb is blocked when tinyusb_host is linked (LIB_TINYUSB_HOST).
 * We implement the driver ourselves: tud_init(0) initialises the native USB
 * hardware controller (USB-C port, rhport 0) as a CDC device; tud_task() is
 * called from kbd_read() on every poll to service control transfers.
 * Register via cdc_stdio_init() and connect a serial terminal to the USB-C
 * port to see printf() output.
 */
/* DVI_ENABLE_CDC: 1 = USB-C serial console (tud device stack + CDC stdio);
 * 0 = device stack removed entirely (UART-only stdio), for isolating whether
 * the native USB device stack / its USBCTRL_IRQ contributes to the intermittent
 * room-transition crash.  The PIO-USB keyboard (tuh host) is unaffected. */
#ifndef DVI_ENABLE_CDC
#define DVI_ENABLE_CDC 1
#endif

#if DVI_ENABLE_CDC
static void cdc_out_chars(const char *buf, int len) {
    if (!tud_cdc_connected()) return;
    /* NON-BLOCKING: printf() must never be able to stall the game.  If the host
     * isn't draining the CDC fast enough the TX buffer fills; rather than spin
     * (which froze core0 under heavy debug output), we write what fits and drop
     * the rest.  Do NOT call tud_task() here — it isn't reentrant and printf can
     * be invoked from IRQ context; tud_task() runs from kbd_read() in the main
     * loop, which is what actually drains the buffer. */
    uint32_t avail = tud_cdc_write_available();
    uint32_t n = (uint32_t)len < avail ? (uint32_t)len : avail;
    if (n) tud_cdc_write(buf, n);
    tud_cdc_write_flush();
}

static stdio_driver_t cdc_stdio_drv = {
    .out_chars = cdc_out_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = true,
#endif
};
#endif /* DVI_ENABLE_CDC */

void cdc_stdio_init(void) {
#if DVI_ENABLE_CDC
    stdio_set_driver_enabled(&cdc_stdio_drv, true);
#endif
}

void kbd_input_init(void) {
    /* DVI (core1) owns DMA_IRQ_0 exclusively.  Switch FatFs_SPI to DMA_IRQ_1
       so its DMA completion interrupts don't conflict with the DVI handler.
       Must be called before sd_card_init() / f_mount(). */
    set_spi_dma_irq_channel(true, true);

#if RP2040_PIZERO
    /* Native USB controller as HID host (rhport 0). No PIO-USB, no board_init(). */
    tuh_init(0);
#else
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp     = PICO_DEFAULT_PIO_USB_DP_PIN; /* GPIO 28; PIO_USB_DEFAULT_CONFIG defaults to 0 */
    pio_cfg.pio_tx_num = 1;  /* pio0 is DVI */
    pio_cfg.pio_rx_num = 1;
    pio_cfg.tx_ch = 7;       /* DVI occupies DMA channels 0-5 */
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    tuh_init(1);
#if DVI_ENABLE_CDC
    tud_init(0); /* USB CDC device on native hardware controller (USB-C port) */
#endif
#endif /* RP2040_PIZERO */
}

int kbd_read(void) {
    tuh_task();
#if DVI_ENABLE_CDC
    tud_task(); /* service USB CDC device */
#endif
    if (key_head == key_tail) return -1;
    int k = key_buf[key_tail];
    key_tail = (key_tail + 1) % KEY_BUF_SIZE;
    return k;
}

static int hid_to_agi(uint8_t hid, uint8_t mod) {
    int shift = mod & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);

    if (hid >= HID_KEY_A && hid <= HID_KEY_Z)
        return shift ? ('A' + hid - HID_KEY_A) : ('a' + hid - HID_KEY_A);

    /* Digits 1-9, 0 (HID 0x1E-0x27) */
    static const char num_plain[] = "1234567890";
    static const char num_shift[] = "!@#$%^&*()";
    if (hid >= HID_KEY_1 && hid <= HID_KEY_0)
        return shift ? num_shift[hid - HID_KEY_1] : num_plain[hid - HID_KEY_1];

    switch (hid) {
        case HID_KEY_ENTER:          return KB_ENTER_CODE;
        case HID_KEY_ESCAPE:         return 0x1B;
        case HID_KEY_BACKSPACE:      return 0x08;
        case HID_KEY_TAB:            return 0x09;
        case HID_KEY_SPACE:          return ' ';
        case HID_KEY_MINUS:          return shift ? '_'  : '-';
        case HID_KEY_EQUAL:          return shift ? '+'  : '=';
        case HID_KEY_BRACKET_LEFT:   return shift ? '{'  : '[';
        case HID_KEY_BRACKET_RIGHT:  return shift ? '}'  : ']';
        case HID_KEY_BACKSLASH:      return shift ? '|'  : '\\';
        case HID_KEY_SEMICOLON:      return shift ? ':'  : ';';
        case HID_KEY_APOSTROPHE:     return shift ? '"'  : '\'';
        case HID_KEY_GRAVE:          return shift ? '~'  : '`';
        case HID_KEY_COMMA:          return shift ? '<'  : ',';
        case HID_KEY_PERIOD:         return shift ? '>'  : '.';
        case HID_KEY_SLASH:          return shift ? '?'  : '/';
        case HID_KEY_ARROW_UP:       return 0xB5;
        case HID_KEY_ARROW_DOWN:     return 0xB6;
        case HID_KEY_ARROW_LEFT:     return 0xB4;
        case HID_KEY_ARROW_RIGHT:    return 0xB7;
        case HID_KEY_HOME:           return 0xD2;
        case HID_KEY_INSERT:         return 0xD1;
        case HID_KEY_DELETE:         return 0xD4;
        case HID_KEY_F1:             return 0x81;
        case HID_KEY_F2:             return 0x82;
        case HID_KEY_F3:             return 0x83;
        case HID_KEY_F4:             return 0x84;
        case HID_KEY_F5:             return 0x85;
        case HID_KEY_F6:             return 0x86;
        case HID_KEY_F7:             return 0x87;
        case HID_KEY_F8:             return 0x88;
        case HID_KEY_F9:             return 0x89;
        case HID_KEY_F10:            return KB_F10_CODE;
        default:                     return -1;
    }
}

/* Track previous report to emit only newly-pressed keys. */
#define MAX_DEV_ADDR 5
#define MAX_INSTANCE 4
static bool kbd_instance[MAX_DEV_ADDR][MAX_INSTANCE];
static uint8_t prev_keycodes[6];
static uint8_t prev_modifier;

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                       uint8_t const *desc_report, uint16_t desc_len) {
    (void)desc_report; (void)desc_len;
    if (dev_addr < MAX_DEV_ADDR && instance < MAX_INSTANCE) {
        uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
        kbd_instance[dev_addr][instance] = (proto == HID_ITF_PROTOCOL_KEYBOARD);
        if (kbd_instance[dev_addr][instance])
            tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_BOOT);
    }
    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (dev_addr < MAX_DEV_ADDR && instance < MAX_INSTANCE)
        kbd_instance[dev_addr][instance] = false;
    memset(prev_keycodes, 0, sizeof(prev_keycodes));
    prev_modifier = 0;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                  uint8_t const *report, uint16_t len) {
    if (dev_addr < MAX_DEV_ADDR && instance < MAX_INSTANCE &&
        kbd_instance[dev_addr][instance] &&
        len >= (uint16_t)sizeof(hid_keyboard_report_t)) {

        hid_keyboard_report_t const *kbd = (hid_keyboard_report_t const *)report;
        for (int i = 0; i < 6; i++) {
            uint8_t kc = kbd->keycode[i];
            if (kc == 0 || kc == 1) continue;  /* 0=none, 1=overflow */
            bool was_down = false;
            for (int j = 0; j < 6; j++)
                if (prev_keycodes[j] == kc) { was_down = true; break; }
            if (!was_down) {
                int agi_k = hid_to_agi(kc, kbd->modifier);
                if (agi_k >= 0) key_push(agi_k);
            }
        }
        memcpy(prev_keycodes, kbd->keycode, 6);
        prev_modifier = kbd->modifier;
    }
    tuh_hid_receive_report(dev_addr, instance);
}
