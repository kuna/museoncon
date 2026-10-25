/*
 * usb_descriptors.c - USB descriptors for BitCon.
 *
 * Every mode exposes a keyboard interface, a gamepad interface, or both,
 * followed by a CDC serial port for the configurator. The layout is picked
 * at boot from the stored output mode. HID runs at bInterval = 1, i.e. 1 ms
 * polling, which is the ceiling for USB Full Speed.
 */

#include <string.h>

#include "tusb.h"
#include "pico/unique_id.h"

#include "config.h"
#include "settings.h"
#include "usb_descriptors.h"

/* =============================================================
 *  Device descriptor
 * ============================================================= */

#define USB_VID 0xCAFE  /* Placeholder. Get a real VID/PID before distributing. */

/*
 * Each output mode gets its own PID.
 *
 * Windows caches HID descriptors against VID/PID. Switching mode while
 * keeping the same PID leaves the stale descriptor in place and the device
 * misbehaves in ways that look like a firmware bug. Separate PIDs make a
 * mode change look like a different device, which it effectively is.
 */
static uint16_t usb_pid(void)
{
    switch (settings_mode()) {
    case OUT_KEYBOARD: return 0xB170;
    case OUT_GAMEPAD:  return 0xB171;
    default:           return 0xB172;
    }
}

static tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    /* Interface Association Descriptors, required by the CDC function. */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = 0,  /* set from the mode at enumeration */

    /* Bumped whenever the interface layout changes, for the same reason
     * as the per-mode PIDs. 0x0200 added the CDC port. */
    .bcdDevice          = 0x0200,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void)
{
    desc_device.idProduct = usb_pid();
    return (uint8_t const *)&desc_device;
}

/* =============================================================
 *  Report descriptors
 * ============================================================= */

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

/*
 * Gamepad: 16 buttons plus X and Y, packed into 4 bytes.
 *
 * Y carries the turntable. X is declared but held centred - some
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

/*
 * TinyUSB numbers HID instances in the order the interfaces appear in the
 * configuration descriptor, so the keyboard always comes first.
 */
int usb_hid_keyboard_instance(void)
{
    return settings_mode() == OUT_GAMEPAD ? -1 : 0;
}

int usb_hid_gamepad_instance(void)
{
    switch (settings_mode()) {
    case OUT_GAMEPAD:   return 0;
    case OUT_COMPOSITE: return 1;
    default:            return -1;
    }
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return (int)instance == usb_hid_keyboard_instance() ? desc_hid_keyboard
                                                        : desc_hid_gamepad;
}

/* =============================================================
 *  Configuration descriptors
 * ============================================================= */

enum {
    STR_LANGID,
    STR_MANUFACTURER,
    STR_PRODUCT,
    STR_SERIAL,
    STR_CDC,
};

#define EPNUM_HID_1      0x81
#define EPNUM_HID_2      0x82
#define EPNUM_CDC_NOTIF  0x83
#define EPNUM_CDC_OUT    0x04
#define EPNUM_CDC_IN     0x84

/*
 * Measured draw with 3.3 V bare LEDs is about 86 mA at the USB port, so
 * 150 mA is declared with headroom. If you drive 5 V arcade LED assemblies
 * (roughly 20 mA each) straight from VBUS, raise this past 250.
 */
#define POWER_MA 150

#define CONFIG_ATTR      TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP
#define CONFIG_LEN_ONE   (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)
#define CONFIG_LEN_BOTH  (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)

/* The CDC function takes two interfaces: control and data. */
#define CDC_DESCRIPTOR(itf) \
    TUD_CDC_DESCRIPTOR(itf, STR_CDC, EPNUM_CDC_NOTIF, 8, \
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, CFG_TUD_CDC_EP_BUFSIZE)

static uint8_t const desc_config_keyboard[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, CONFIG_LEN_ONE, CONFIG_ATTR, POWER_MA),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_keyboard),
                       EPNUM_HID_1, CFG_TUD_HID_EP_BUFSIZE, 1),
    CDC_DESCRIPTOR(1),
};

static uint8_t const desc_config_gamepad[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, CONFIG_LEN_ONE, CONFIG_ATTR, POWER_MA),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_gamepad),
                       EPNUM_HID_1, CFG_TUD_HID_EP_BUFSIZE, 1),
    CDC_DESCRIPTOR(1),
};

static uint8_t const desc_config_composite[] = {
    TUD_CONFIG_DESCRIPTOR(1, 4, 0, CONFIG_LEN_BOTH, CONFIG_ATTR, POWER_MA),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_keyboard),
                       EPNUM_HID_1, CFG_TUD_HID_EP_BUFSIZE, 1),
    TUD_HID_DESCRIPTOR(1, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_gamepad),
                       EPNUM_HID_2, CFG_TUD_HID_EP_BUFSIZE, 1),
    CDC_DESCRIPTOR(2),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    switch (settings_mode()) {
    case OUT_KEYBOARD: return desc_config_keyboard;
    case OUT_GAMEPAD:  return desc_config_gamepad;
    default:           return desc_config_composite;
    }
}

/* =============================================================
 *  String descriptors
 * ============================================================= */

static char serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

static char const *product_str(void)
{
    switch (settings_mode()) {
    case OUT_KEYBOARD: return "BitCon (keyboard)";
    case OUT_GAMEPAD:  return "BitCon (gamepad)";
    default:           return "BitCon (composite)";
    }
}

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint8_t chr_count;

    if (index == STR_LANGID) {
        _desc_str[1] = 0x0409;
        chr_count = 1;
    } else {
        const char *str;
        switch (index) {
        case STR_MANUFACTURER: str = "DIY";            break;
        case STR_PRODUCT:      str = product_str();    break;
        case STR_SERIAL:
            if (serial_str[0] == '\0') {
                pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
            }
            str = serial_str;
            break;
        case STR_CDC:          str = "BitCon Config";  break;
        default:               return NULL;
        }

        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
