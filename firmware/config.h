/*
 * config.h - BitCon hardware map and tuning parameters.
 *
 * This is the only file you should need to edit. It deliberately has no
 * TinyUSB or pico-sdk dependency, so any source file can include it.
 *
 * The pin assignments match the schematic 1:1.
 */

#ifndef BITCON_CONFIG_H
#define BITCON_CONFIG_H

#include <stdint.h>

/* =============================================================
 *  Output mode
 * ============================================================= */

/*
 * Games read the turntable in incompatible ways, so the gamepad comes in
 * three flavours, one per game family:
 *
 *   OUT_LR2       - turntable as two buttons. LR2 only accepts a digital
 *                   turntable: neutral, clockwise, counter-clockwise.
 *   OUT_BEATORAJA - turntable as a digital X axis: full scale while the disc
 *                   spins, centred when it stops. beatoraja's default input
 *                   path (analog scratch off) treats an axis past +-0.9 as a
 *                   pressed scratch.
 *   OUT_INFINITAS - turntable as an analog X axis holding the disc position,
 *                   wrapping every 256 units, and the rest of the report
 *                   laid out like the official controller, whose USB ID
 *                   it borrows (experimental, see INFINITAS mode below).
 *                   Also suits beatoraja with analog scratch on.
 *
 * OUT_INFINITAS keeps the value the single gamepad mode used to have, so a
 * board that stored "gamepad" keeps the same behaviour.
 */
#define OUT_KEYBOARD   1  /* NKRO keyboard only                        */
#define OUT_INFINITAS  2  /* Gamepad, analog turntable axis            */
#define OUT_COMPOSITE  3  /* Keyboard + INFINITAS-style gamepad        */
#define OUT_LR2        4  /* Gamepad, turntable as two buttons         */
#define OUT_BEATORAJA  5  /* Gamepad, digital turntable axis           */

/*
 * The output mode is a runtime setting: the configurator page changes it
 * over the serial port and it is kept in flash across reconnects and
 * firmware updates. This is only the mode a board boots in before anything
 * has been stored.
 *
 * Override from the build system with -DDEFAULT_OUTPUT_MODE=OUT_KEYBOARD if
 * you prefer not to edit this file.
 */
#ifndef DEFAULT_OUTPUT_MODE
#define DEFAULT_OUTPUT_MODE  OUT_BEATORAJA
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
 * LR2 and beatoraja modes: buttons 1..9 map straight through to gamepad
 * buttons 1..9. In LR2 mode the turntable adds buttons 10 (clockwise) and
 * 11. The descriptor declares 16 buttons, so there is room to grow.
 * INFINITAS mode uses its own layout; see below.
 */
#define GAMEPAD_BUTTON_COUNT 16

/* =============================================================
 *  INFINITAS mode (experimental)
 * ============================================================= */

/*
 * INFINITAS mode reports as Konami's official INFINITAS controller (VID
 * 0x1CCF, PID 0x8048), as the arcin firmware does. INFINITAS recognises it
 * and applies its own fixed key layout, so its in-game key config is
 * neither needed nor honoured: press "デフォルトに戻す" (reset to default)
 * in its settings once.
 *
 * These are Konami's USB IDs, used here only so the game talks to the
 * controller.
 */

/*
 * Button bits of the official controller's report: keys 1-7 on buttons 1-7
 * and E1-E4 on buttons 9-12, per arcin. Button 8 is unused.
 */
#define INF_KEY1  0
#define INF_KEY2  1
#define INF_KEY3  2
#define INF_KEY4  3
#define INF_KEY5  4
#define INF_KEY6  5
#define INF_KEY7  6
#define INF_E1    8
#define INF_E2    9
#define INF_E3    10
#define INF_E4    11

/*
 * Which INFINITAS input each BitCon button drives, in button order.
 *
 * Assumes the top plate's layout: the zig-zag of seven large buttons
 * (2-8) are keys 1-7, white keys below and black keys above, and the two
 * small square buttons at the ends (1 and 9) are E1 and E2. Swap entries
 * here if your buttons sit differently. E3 and E4 stay unassigned.
 */
#define INFINITAS_BUTTON_LIST {                          \
    INF_E1,                                              \
    INF_KEY1, INF_KEY2, INF_KEY3, INF_KEY4,              \
    INF_KEY5, INF_KEY6, INF_KEY7,                        \
    INF_E2,                                              \
}

/* =============================================================
 *  Input pull-ups
 * ============================================================= */

/*
 * Both input groups are active low: the switch shorts its pin to ground,
 * and the phototransistor sinks its pin to ground. Something has to hold
 * the line high the rest of the time, or the pin floats, reads as
 * permanently pressed, and picks up mains hum and crosstalk from the
 * neighbouring LED wires.
 *
 * These enable the RP2350 internal pull-ups, the equivalent of Arduino's
 * INPUT_PULLUP. They default to on so the firmware works on a board where
 * the external resistors are missing, mis-wired, or a harness wire has
 * broken. Leaving them on alongside correctly fitted external resistors is
 * harmless: 10k in parallel with the internal ~55k gives about 8.5k, which
 * changes nothing that matters.
 */
#ifndef USE_INTERNAL_PULLUP_BTN
#define USE_INTERNAL_PULLUP_BTN 1
#endif

/*
 * The sensor case differs in kind, not just degree.
 *
 * A phototransistor is open collector: it can only pull down, so the
 * pull-up is the only thing that ever creates a logic high. The internal
 * one will make it work, but at roughly 55k the rise time stretches into
 * tens of microseconds, smearing edges at high turntable speed and
 * eventually dropping counts. Treat this as a bring-up crutch and fit the
 * external 4.7k resistors before tuning anything.
 */
#ifndef USE_INTERNAL_PULLUP_SEN
#define USE_INTERNAL_PULLUP_SEN 1
#endif

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
 * Digital turntable: LR2 and beatoraja modes, and the keyboard's Q / W.
 *
 * A direction registers once the disc has moved TT_DIGITAL_STEP counts, and
 * stays held while it keeps moving at least that far per sustain window. It
 * releases one window after the last step, so creep slower than one step
 * per window never registers and +-1 count jitter never does either.
 * Reversing switches straight to the other direction; both are never held
 * at once.
 *
 * LR2 uses arcin's 200 ms, which fixed LR2 misfires on full-size
 * turntables. beatoraja uses DJ DAO's default 100 ms trigger window, which
 * releases sooner for backspin scratch ends. The keyboard follows LR2.
 */
#ifndef TT_DIGITAL_STEP
#define TT_DIGITAL_STEP 2                /* of 192 counts: 3.75 degrees */
#endif

#ifndef TT_LR2_SUSTAIN_US
#define TT_LR2_SUSTAIN_US 200000
#endif

#ifndef TT_BEATORAJA_SUSTAIN_US
#define TT_BEATORAJA_SUSTAIN_US 100000
#endif

/*
 * Analog turntable: INFINITAS and composite modes.
 *
 * The X axis carries the disc position and wraps every 256 units. IIDX
 * registers a scratch only when the axis moved at least 2 units against
 * its value about two frames (~33 ms) earlier, so 2 units per count lets
 * every encoder count reach that threshold and keeps slow scratches alive.
 *
 * Reversals must travel TT_ANALOG_REVERSE_COUNTS before the axis follows.
 * Without this, a disc resting on a slot edge flickers +-1 count, which at
 * 2 units per count reads as a scratch.
 */
#ifndef TT_ANALOG_UNITS_PER_COUNT
#define TT_ANALOG_UNITS_PER_COUNT 2
#endif

#ifndef TT_ANALOG_REVERSE_COUNTS
#define TT_ANALOG_REVERSE_COUNTS 2
#endif

/*
 * Flip the analog axis direction. Scratch notes do not care which way the
 * disc turns, but song-select scrolling does: set to 1 if it runs
 * backwards. arcin offers the same as its "analog reversed" mode.
 */
#ifndef TT_ANALOG_REVERSE
#define TT_ANALOG_REVERSE 0
#endif

/*
 * Quadrature counts per full revolution of the encoder disc. Only the
 * configurator uses it, to draw the disc and count turns.
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

#if (DEFAULT_OUTPUT_MODE < OUT_KEYBOARD) || (DEFAULT_OUTPUT_MODE > OUT_BEATORAJA)
  #error "DEFAULT_OUTPUT_MODE must be one of the OUT_* modes"
#endif

#if TT_DIGITAL_STEP < 2
  #error "TT_DIGITAL_STEP below 2 lets +-1 count jitter register as a scratch"
#endif

#if TT_COUNTS_PER_REV < 4
  #error "TT_COUNTS_PER_REV must be at least 4"
#endif

#endif /* BITCON_CONFIG_H */
