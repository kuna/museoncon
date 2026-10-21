/*
 * usb_descriptors.h - HID instance numbers for the running output mode.
 */

#ifndef BITCON_USB_DESCRIPTORS_H
#define BITCON_USB_DESCRIPTORS_H

/* TinyUSB HID instance of the keyboard or gamepad interface, or -1 when the
 * current mode does not expose it. */
int usb_hid_keyboard_instance(void);
int usb_hid_gamepad_instance(void);

#endif /* BITCON_USB_DESCRIPTORS_H */
