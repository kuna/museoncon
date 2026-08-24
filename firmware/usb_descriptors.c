/*
 * usb_descriptors.c - HID descriptors for BitCon.
 *
 * Builds a keyboard interface, a gamepad interface, or both, according to
 * OUTPUT_MODE in config.h. Both run at bInterval = 1, i.e. 1 ms polling,
 * which is the ceiling for USB Full Speed.
 */

#include <string.h>

#include "tusb.h"
#include "pico/unique_id.h"

#include "config.h"

/* =============================================================
 *  Device descriptor
 * ============================================================= */

#define USB_VID 0xCAFE  /* Placeholder. Get a real VID/PID before distributing. */

/*
 * Each output mode gets its own PID.
 *
 * Windows caches HID descriptors against VID/PID. Reflashing a different
 * mode while keeping the same PID leaves the stale descriptor in place and
 * the device misbehaves in ways that look like a firmware bug. Separate PIDs
 * make a mode change look like a different device, which it effectively is.
 */
#if OUTPUT_MODE == OUT_KEYBOARD
  #define USB_PID      0xB170
  #define PRODUCT_STR  "BitCon (keyboard)"
#elif OUTPUT_MODE == OUT_GAMEPAD
  #define USB_PID      0xB171
  #define PRODUCT_STR  "BitCon (gamepad)"
#else
  #define USB_PID      0xB172
  #define PRODUCT_STR  "BitCon (composite)"
#endif

static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

/* =============================================================
 *  Report descriptors
 * ============================================================= */

#if BITCON_HAS_KEYBOARD
/*
 * NKRO bitmap keyboard: one bit per keycode from 0x00 to 0x7F, 16 bytes
 * total. No 6-key-rollover limit. Modifiers (0xE0-0xE7) fall inside the
 * same range, so they need no separate handling.
 */
static uint8_t const desc_hid_keyboard[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop)     */
    0x09, 0x06,        /* Usage (Keyboard)                 */
    0xA1, 0x01,        /* Collection (Application)         */
    0x05, 0x07,        /*   Usage Page (Keyboard/Keypad)   */
    0x19, 0x00,        /*   Usage Minimum (0)              */
    0x29, 0x7F,        /*   Usage Maximum (127)            */
    0x15, 0x00,        /*   Logical Minimum (0)            */
    0x25, 0x01,        /*   Logical Maximum (1)            */
    0x75, 0x01,        /*   Report Size (1)                */
    0x95, 0x80,        /*   Report Count (128)             */
    0x81, 0x02,        /*   Input (Data, Var, Abs)         */
    0xC0               /* End Collection                   */
};
#endif

#if BITCON_HAS_GAMEPAD
/*
 * Gamepad: 16 buttons plus X and Y, packed into 4 bytes.
 *
 * X carries the turntable. Y is declared but held centred - some
 * DirectInput drivers will not enumerate a single-axis controller.
 */
static uint8_t const desc_hid_gamepad[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop)     */
    0x09, 0x05,        /* Usage (Game Pad)                 */
    0xA1, 0x01,        /* Collection (Application)         */

    0x05, 0x09,        /*   Usage Page (Button)            */
    0x19, 0x01,        /*   Usage Minimum (Button 1)       */
    0x29, GAMEPAD_BUTTON_COUNT, /*   Usage Maximum         */
    0x15, 0x00,        /*   Logical Minimum (0)            */
    0x25, 0x01,        /*   Logical Maximum (1)            */
    0x75, 0x01,        /*   Report Size (1)                */
    0x95, GAMEPAD_BUTTON_COUNT, /*   Report Count          */
    0x81, 0x02,        /*   Input (Data, Var, Abs)         */

    0x05, 0x01,        /*   Usage Page (Generic Desktop)   */
    0x09, 0x30,        /*   Usage (X)                      */
    0x09, 0x31,        /*   Usage (Y)                      */
    0x15, 0x00,        /*   Logical Minimum (0)            */
    0x26, 0xFF, 0x00,  /*   Logical Maximum (255)          */
    0x75, 0x08,        /*   Report Size (8)                */
    0x95, 0x02,        /*   Report Count (2)               */
    0x81, 0x02,        /*   Input (Data, Var, Abs)         */

    0xC0               /* End Collection                   */
};
#endif

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
#if OUTPUT_MODE == OUT_COMPOSITE
    return (instance == ITF_KEYBOARD) ? desc_hid_keyboard : desc_hid_gamepad;
#elif OUTPUT_MODE == OUT_KEYBOARD
    (void)instance;
    return desc_hid_keyboard;
#else
    (void)instance;
    return desc_hid_gamepad;
#endif
}

/* =============================================================
 *  Configuration descriptor
 * ============================================================= */

enum {
#if BITCON_HAS_KEYBOARD
    ITF_NUM_KEYBOARD,
#endif
#if BITCON_HAS_GAMEPAD
    ITF_NUM_GAMEPAD,
#endif
    ITF_NUM_TOTAL
};

#if OUTPUT_MODE == OUT_COMPOSITE
  #define EPNUM_KEYBOARD 0x81
  #define EPNUM_GAMEPAD  0x82
  #define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN)
#elif OUTPUT_MODE == OUT_KEYBOARD
  #define EPNUM_KEYBOARD 0x81
  #define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#else
  #define EPNUM_GAMEPAD  0x81
  #define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#endif

/*
 * Measured draw with 3.3 V bare LEDs is about 86 mA at the USB port, so
 * 150 mA is declared with headroom. If you drive 5 V arcade LED assemblies
 * (roughly 20 mA each) straight from VBUS, raise this past 250.
 */
#define POWER_MA 150

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, POWER_MA),

#if BITCON_HAS_KEYBOARD
    TUD_HID_DESCRIPTOR(ITF_NUM_KEYBOARD, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_keyboard), EPNUM_KEYBOARD,
                       CFG_TUD_HID_EP_BUFSIZE, 1),
#endif

#if BITCON_HAS_GAMEPAD
    TUD_HID_DESCRIPTOR(ITF_NUM_GAMEPAD, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_gamepad), EPNUM_GAMEPAD,
                       CFG_TUD_HID_EP_BUFSIZE, 1),
#endif
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

/* =============================================================
 *  String descriptors
 * ============================================================= */

static char serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

static char const *string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },  /* 0: en-US                          */
    "DIY",                         /* 1: manufacturer                   */
    PRODUCT_STR,                   /* 2: product                        */
    serial_str,                    /* 3: serial, from board unique ID   */
};

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint8_t chr_count;

    if (index == 0) {
        _desc_str[1] = 0x0409;
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
            return NULL;
        }

        if (index == 3 && serial_str[0] == '\0') {
            pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
        }

        const char *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
