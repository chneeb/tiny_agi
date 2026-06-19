#pragma once
#include <stdint.h>

// Call once at startup.
void kbd_input_init(void);

// Poll once; returns the key byte or -1 if no key is ready.
// Key codes match the PicoCalc conventions used throughout platform.c:
//   0xB5/B6/B4/B7 = UP/DOWN/LEFT/RIGHT, 0x81-0x8A = F1-F10,
//   0x1B = ESC, 0x08 = backspace, 0x0D = Enter, 0x20-0x7E = ASCII.
int kbd_read(void);
