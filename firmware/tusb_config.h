/*
 * tusb_config.h - TinyUSB configuration.
 *
 * Includes config.h so that CFG_TUD_HID follows OUTPUT_MODE automatically.
 * config.h has no TinyUSB dependency, so this cannot go circular.
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "config.h"

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

/* 1 for keyboard-only or gamepad-only, 2 for composite. */
#define CFG_TUD_HID           HID_INSTANCE_COUNT

#define CFG_TUD_CDC           0
#define CFG_TUD_MSC           0
#define CFG_TUD_MIDI          0
#define CFG_TUD_VENDOR        0

/* Largest report is the 16-byte NKRO keyboard report. */
#define CFG_TUD_HID_EP_BUFSIZE 32

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H */
