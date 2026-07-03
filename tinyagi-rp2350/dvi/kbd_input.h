#pragma once

void kbd_input_init(void);
int  kbd_read(void);
void cdc_stdio_init(void); /* register USB-C CDC port as stdio output */
