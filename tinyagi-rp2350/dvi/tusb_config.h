/* TinyUSB configuration for DVI target — dual role:
 *   rhport 0 = USB device (CDC serial) on the hardware USB controller → USB-C connector
 *   rhport 1 = USB host (HID keyboard) via PIO-USB → USB-A connector (GPIO 28)
 *
 * Do NOT define CFG_TUSB_RHPORT1_MODE here: tusb_init() (called from
 * stdio_init_all) must not touch rhport 1 before kbd_input_init() has
 * called tuh_configure() to set the PIO-USB parameters.
 */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS  OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG  0
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN  __attribute__((aligned(4)))
#endif

/* ── Device (rhport 0, hardware USB → USB-C → CDC serial) ─────────────────── */
#define CFG_TUSB_RHPORT0_MODE  (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUD_ENABLED        1
#define CFG_TUD_CDC            1
#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 256
#define CFG_TUD_MSC            0
#define CFG_TUD_HID            0
#define CFG_TUD_MIDI           0
#define CFG_TUD_VENDOR         0

/* ── Host (rhport 1, PIO-USB → USB-A → HID keyboard) ─────────────────────── */
/* rhport 1 mode is set at runtime by tuh_configure() in kbd_input_init().
   CFG_TUH_ENABLED must be set explicitly so tusb.h pulls in the host headers
   even though CFG_TUSB_RHPORT1_MODE is not defined statically.               */
#define CFG_TUH_ENABLED        1
#define CFG_TUH_RPI_PIO_USB    1
#define BOARD_TUH_RHPORT       1

#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED  OPT_MODE_FULL_SPEED
#endif
#define CFG_TUH_MAX_SPEED  BOARD_TUH_MAX_SPEED

#define CFG_TUH_ENUMERATION_BUFSIZE  256
#define CFG_TUH_HUB     1
#define CFG_TUH_HID     4
#define CFG_TUH_CDC     0
#define CFG_TUH_MSC     0
#define CFG_TUH_VENDOR  0
#define CFG_TUH_DEVICE_MAX  (CFG_TUH_HUB ? 4 : 1)
#define CFG_TUH_HID_EPIN_BUFSIZE   64
#define CFG_TUH_HID_EPOUT_BUFSIZE  64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
