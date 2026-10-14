#ifndef USB_SERIAL_H
#define USB_SERIAL_H

#include <stdint.h>

extern volatile uint32_t ms_ticks;
void usb_serial_init(void);
int usb_serial_getchar(void);

#endif
