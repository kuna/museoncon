/*
 * tusb_config.h - TinyUSB configuration.
 *
 * Sized for the largest layout, composite mode. The other modes simply
 * leave the second HID instance unused.
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* The pico-sdk tinyusb_device library normally defines this for us. */
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU          OPT_MCU_RP2040
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_PICO
#endif

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

/* Root hub port passed to tud_init(). RP2350 has a single USB controller. */
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))
#endif

#define CFG_TUD_ENDPOINT0_SIZE 64

/* Keyboard and gamepad. Single-interface modes use only instance 0. */
#define CFG_TUD_HID           2

/* Configurator serial port. Present in every mode. */
#define CFG_TUD_CDC           1
#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 1024
#define CFG_TUD_CDC_EP_BUFSIZE 64

#define CFG_TUD_MSC           0
#define CFG_TUD_MIDI          0
#define CFG_TUD_VENDOR        0

/* Largest report is the 16-byte NKRO keyboard report. */
#define CFG_TUD_HID_EP_BUFSIZE 32

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H */
