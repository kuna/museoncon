/*
 * config.h - BitCon hardware map and tuning parameters.
 *
 * This is the only file you should need to edit. It deliberately has no
 * TinyUSB dependency so that tusb_config.h can include it to size the
 * HID instance count.
 *
 * The pin assignments match the schematic 1:1.
 */

#ifndef BITCON_CONFIG_H
#define BITCON_CONFIG_H

#include <stdint.h>

/* =============================================================
 *  Output mode
 * ============================================================= */

#define OUT_KEYBOARD   1  /* NKRO keyboard only                        */
#define OUT_GAMEPAD    2  /* Gamepad only                              */
#define OUT_COMPOSITE  3  /* Both, as two independent HID interfaces   */

/* Override from the build system with -DOUTPUT_MODE=OUT_GAMEPAD if you
 * prefer not to edit this file, e.g. to build all three variants in CI. */
#ifndef OUTPUT_MODE
#define OUTPUT_MODE    OUT_COMPOSITE
#endif

/*
 * How the turntable is represented in gamepad mode.
 *
 *   TT_AXIS    - absolute wrapping 8-bit axis. This is what real IIDX
 *                controllers do, and what most rhythm games expect.
 *   TT_DIGITAL - two direction buttons, same behaviour as keyboard mode.
 *
 * Keyboard mode always uses the digital representation; a keyboard has
 * no axes.
 */
#define TT_AXIS        1
#define TT_DIGITAL     2

#ifndef TT_GAMEPAD_STYLE
#define TT_GAMEPAD_STYLE  TT_AXIS
#endif

/* =============================================================
 *  Pin map
 * ============================================================= */

#define NUM_BTN 9

/*
 * Each button's switch and LED sit on adjacent GPIOs so the pair routes
 * side by side on the board. GP15 and GP16 are on opposite header rows,
 * so button 9 starts the other row at GP16 instead of pairing across.
 */

/* Switch inputs - active low, external 10k pull-up. */
#define BTN_PIN_LIST { 0, 2, 4, 6, 8, 10, 12, 14, 16 }

/* LED outputs - active high, 220R in series. */
#define LED_PIN_LIST { 1, 3, 5, 7, 9, 11, 13, 15, 17 }

/*
 * Photointerrupters - active low, external 4.7k pull-up.
 *
 * The quadrature decoder reads both pins in a single GPIO word, so these
 * must be consecutive. GP20 = SEN1 (channel A), GP21 = SEN2 (channel B).
 */
#define PIN_SEN_BASE 20

/* =============================================================
 *  Key / button mapping
 * ============================================================= */

/*
 * Internal key bitmap layout, shared by both output modes:
 *   bits 0..8 - buttons 1..9
 *   bit  9    - turntable clockwise
 *   bit  10   - turntable counter-clockwise
 */
#define KEY_CW_BIT   (NUM_BTN + 0)
#define KEY_CCW_BIT  (NUM_BTN + 1)
#define NUM_KEYS     (NUM_BTN + 2)

/* USB HID Keyboard/Keypad usage IDs (HID Usage Tables 1.12, section 10). */
#define KEY_A          0x04
#define KEY_D          0x07
#define KEY_F          0x09
#define KEY_J          0x0D
#define KEY_K          0x0E
#define KEY_L          0x0F
#define KEY_Q          0x14
#define KEY_S          0x16
#define KEY_W          0x1A
#define KEY_SPACE      0x2C
#define KEY_SEMICOLON  0x33

/* Keyboard mode: one usage ID per bit of the key bitmap. */
#define KEYMAP_LIST {           \
    KEY_A,          /* btn 1 */ \
    KEY_S,          /* btn 2 */ \
    KEY_D,          /* btn 3 */ \
    KEY_F,          /* btn 4 */ \
    KEY_SPACE,      /* btn 5 */ \
    KEY_J,          /* btn 6 */ \
    KEY_K,          /* btn 7 */ \
    KEY_L,          /* btn 8 */ \
    KEY_SEMICOLON,  /* btn 9 */ \
    KEY_Q,          /* turntable CW  */ \
    KEY_W,          /* turntable CCW */ \
}

/*
 * Gamepad mode: buttons 1..9 map straight through to gamepad buttons 1..9.
 * When TT_GAMEPAD_STYLE is TT_DIGITAL the turntable adds buttons 10 and 11.
 * The descriptor declares 16 buttons, so there is room to grow.
 */
#define GAMEPAD_BUTTON_COUNT 16

/* =============================================================
 *  Timing
 * ============================================================= */

/*
 * Eager debounce lockout.
 *
 * A state change is applied immediately, then further changes are ignored
 * for this long. Press latency is zero, which is the whole point for a
 * rhythm controller. Raise to 2000-4000 if you see chatter.
 */
#ifndef DEBOUNCE_US
#define DEBOUNCE_US 1000
#endif

/*
 * Quadrature counts required to emit one turntable direction event.
 * Acts as hysteresis: jitter smaller than this never produces a keypress.
 * Used by keyboard mode and by TT_DIGITAL gamepad mode.
 */
#ifndef TT_STEP
#define TT_STEP 2
#endif

/* How long a turntable direction key stays held after an event. */
#ifndef TT_HOLD_US
#define TT_HOLD_US 40000  /* 40 ms */
#endif

/*
 * Quadrature counts per full revolution of the encoder disc, used to scale
 * the gamepad axis so that one physical spin is one full 0-255 sweep.
 *
 * Set this to 4 x (number of slots on the disc). Quadrature gives four
 * counts per slot: two edges per channel, two channels.
 */
#ifndef TT_COUNTS_PER_REV
#define TT_COUNTS_PER_REV (4 * 48)
#endif

/* =============================================================
 *  Derived - do not edit below this line
 * ============================================================= */

#if OUTPUT_MODE == OUT_COMPOSITE
  #define BITCON_HAS_KEYBOARD 1
  #define BITCON_HAS_GAMEPAD  1
  #define ITF_KEYBOARD        0
  #define ITF_GAMEPAD         1
  #define HID_INSTANCE_COUNT  2
#elif OUTPUT_MODE == OUT_KEYBOARD
  #define BITCON_HAS_KEYBOARD 1
  #define BITCON_HAS_GAMEPAD  0
  #define ITF_KEYBOARD        0
  #define HID_INSTANCE_COUNT  1
#elif OUTPUT_MODE == OUT_GAMEPAD
  #define BITCON_HAS_KEYBOARD 0
  #define BITCON_HAS_GAMEPAD  1
  #define ITF_GAMEPAD         0
  #define HID_INSTANCE_COUNT  1
#else
  #error "OUTPUT_MODE must be OUT_KEYBOARD, OUT_GAMEPAD or OUT_COMPOSITE"
#endif

#if BITCON_HAS_GAMEPAD && \
    (TT_GAMEPAD_STYLE != TT_AXIS) && (TT_GAMEPAD_STYLE != TT_DIGITAL)
  #error "TT_GAMEPAD_STYLE must be TT_AXIS or TT_DIGITAL"
#endif

#if TT_COUNTS_PER_REV < 4
  #error "TT_COUNTS_PER_REV must be at least 4"
#endif

#endif /* BITCON_CONFIG_H */
