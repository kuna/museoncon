/*
 * main.c - BitCon firmware for RP2350 (Raspberry Pi Pico 2).
 *
 * 9 illuminated buttons + a 2-channel photointerrupter turntable, exposed
 * as a USB HID keyboard, gamepad, or both. See config.h.
 *
 * Core split:
 *   core1 - free-running input scan, debounce, quadrature decode, LEDs.
 *   core0 - TinyUSB stack and HID report generation.
 *
 * The two cores communicate through two 32-bit words. RP2350 SRAM is
 * uncached and aligned 32-bit accesses are atomic, so no lock is needed.
 * The words are sampled independently, so buttons and turntable position
 * can be up to one core1 iteration apart - sub-microsecond, irrelevant here.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "tusb.h"

#include "config.h"

/* =============================================================
 *  Shared state
 * ============================================================= */

/* Bits 0..NUM_KEYS-1 of the internal key bitmap. Written by core1. */
static volatile uint32_t g_key_state = 0;

/* Accumulated quadrature position. Written by core1. */
static volatile int32_t g_tt_pos = 0;

/* =============================================================
 *  Quadrature decode table
 * ============================================================= */

/*
 * Index = (previous_state << 2) | current_state, each state being (B << 1) | A.
 *
 * Forward Gray sequence: 00 -> 01 -> 11 -> 10 -> 00.
 * Invalid transitions (both bits changing at once, which means a sample was
 * missed or the signal glitched) decode to 0 and are simply dropped.
 */
static const int8_t QDEC[16] = {
     0, +1, -1,  0,
    -1,  0,  0, +1,
    +1,  0,  0, -1,
     0, -1, +1,  0,
};

/* =============================================================
 *  GPIO
 * ============================================================= */

static const uint8_t BTN_PINS[NUM_BTN] = BTN_PIN_LIST;
static const uint8_t LED_PINS[NUM_BTN] = LED_PIN_LIST;

static uint32_t g_led_mask = 0;

static void gpio_setup(void)
{
    for (int i = 0; i < NUM_BTN; i++) {
        /*
         * Switch input. The board has an external 10k pull-up, so the
         * internal one stays off - enabling it would put ~55k in parallel
         * and quietly change the circuit away from what the schematic says.
         */
        gpio_init(BTN_PINS[i]);
        gpio_set_dir(BTN_PINS[i], GPIO_IN);
        gpio_set_pulls(BTN_PINS[i], false, false);
        gpio_set_input_hysteresis_enabled(BTN_PINS[i], true);

        /*
         * LED output. Drive strength has to go up from the 4 mA default,
         * otherwise the output droops when all nine light at once and the
         * brightness becomes uneven.
         *
         * Slew rate goes down: LEDs do not need fast edges, and slower
         * edges put less switching noise onto the neighbouring switch lines
         * inside the harness.
         */
        gpio_init(LED_PINS[i]);
        gpio_set_dir(LED_PINS[i], GPIO_OUT);
        gpio_set_drive_strength(LED_PINS[i], GPIO_DRIVE_STRENGTH_8MA);
        gpio_set_slew_rate(LED_PINS[i], GPIO_SLEW_RATE_SLOW);
        gpio_put(LED_PINS[i], 0);

        g_led_mask |= (1u << LED_PINS[i]);
    }

    for (int i = 0; i < 2; i++) {
        const uint8_t pin = PIN_SEN_BASE + i;
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_set_pulls(pin, false, false);
        gpio_set_input_hysteresis_enabled(pin, true);
    }
}

/* =============================================================
 *  core1 - input scan
 * ============================================================= */

static void core1_main(void)
{
    /*
     * Timestamps must be 64-bit.
     *
     * time_us_32() wraps every 71.6 minutes, and the usual signed-difference
     * trick ((int32_t)(now - deadline) >= 0) only holds while the two values
     * are within 2^31 of each other. A button that has never been pressed
     * keeps lock_until at 0, so 35.8 minutes after boot the difference flips
     * negative and that button stops responding entirely. The turntable hold
     * fails the other way: the comparison becomes true and the key sticks on.
     *
     * time_us_64() overflows in about 584,000 years and is multicore-safe
     * (it reads TIMERAWH/TIMERAWL in a retry loop), which removes the entire
     * class of bug rather than patching one instance of it.
     */
    uint64_t lock_until[NUM_BTN] = { 0 };
    bool     btn_state[NUM_BTN]  = { false };

    int32_t  tt_pos = 0;              /* accumulated position            */
    int32_t  tt_ref = 0;              /* position at last emitted event  */
    uint64_t tt_hold[2] = { 0, 0 };   /* [0] = CW, [1] = CCW             */

    uint8_t q_prev = (uint8_t)((gpio_get_all() >> PIN_SEN_BASE) & 0x3u);

    while (true) {
        const uint64_t now_us = time_us_64();
        const uint32_t gpio   = gpio_get_all();

        uint32_t keys    = 0;
        uint32_t led_val = 0;

        /* ---- buttons: eager debounce ---- */
        for (int i = 0; i < NUM_BTN; i++) {
            const bool raw = ((gpio >> BTN_PINS[i]) & 1u) == 0u;  /* active low */

            if (raw != btn_state[i] && now_us >= lock_until[i]) {
                btn_state[i]  = raw;
                lock_until[i] = now_us + DEBOUNCE_US;
            }

            if (btn_state[i]) {
                keys    |= (1u << i);
                led_val |= (1u << LED_PINS[i]);
            }
        }

        /* One write for all nine LEDs; other GPIOs are left alone. */
        gpio_put_masked(g_led_mask, led_val);

        /* ---- turntable: quadrature decode ---- */
        const uint8_t q = (uint8_t)((gpio >> PIN_SEN_BASE) & 0x3u);
        if (q != q_prev) {
            tt_pos += QDEC[(q_prev << 2) | q];
            q_prev  = q;
        }

        /*
         * Emit a direction event once accumulated travel exceeds TT_STEP,
         * then advance the reference by that much. The gap between tt_pos
         * and tt_ref acts as hysteresis, so a disc resting on a boundary
         * cannot rattle both directions at once.
         */
        const int32_t delta = tt_pos - tt_ref;
        if (delta >= TT_STEP) {
            tt_ref    += TT_STEP;
            tt_hold[0] = now_us + TT_HOLD_US;
        } else if (delta <= -TT_STEP) {
            tt_ref    -= TT_STEP;
            tt_hold[1] = now_us + TT_HOLD_US;
        }

        if (tt_hold[0] > now_us) keys |= (1u << KEY_CW_BIT);
        if (tt_hold[1] > now_us) keys |= (1u << KEY_CCW_BIT);

        g_key_state = keys;
        g_tt_pos    = tt_pos;
    }
}

/* =============================================================
 *  core0 - keyboard reports
 * ============================================================= */

#if BITCON_HAS_KEYBOARD

/*
 * NKRO bitmap report: 128 bits, one per keycode. Unlike the 6-key-rollover
 * boot keyboard this places no limit on simultaneous input, so nine buttons
 * plus both scratch directions all register together.
 */
#define KBD_REPORT_BYTES 16

static const uint8_t KEYMAP[NUM_KEYS] = KEYMAP_LIST;

static void keyboard_task(void)
{
    if (!tud_hid_n_ready(ITF_KEYBOARD)) return;

    const uint32_t state = g_key_state;

    uint8_t report[KBD_REPORT_BYTES] = { 0 };
    for (int i = 0; i < NUM_KEYS; i++) {
        if (state & (1u << i)) {
            const uint8_t kc = KEYMAP[i];
            report[kc >> 3] |= (uint8_t)(1u << (kc & 7u));
        }
    }

    static uint8_t prev[KBD_REPORT_BYTES];
    static bool    primed = false;

    if (primed && memcmp(report, prev, sizeof(report)) == 0) return;

    if (tud_hid_n_report(ITF_KEYBOARD, 0, report, sizeof(report))) {
        memcpy(prev, report, sizeof(report));
        primed = true;
    }
}

#endif /* BITCON_HAS_KEYBOARD */

/* =============================================================
 *  core0 - gamepad reports
 * ============================================================= */

#if BITCON_HAS_GAMEPAD

typedef struct __attribute__((packed)) {
    uint16_t buttons;  /* 16 buttons, bit 0 = button 1 */
    uint8_t  x;        /* turntable */
    uint8_t  y;        /* unused, held centred */
} gamepad_report_t;

#if TT_GAMEPAD_STYLE == TT_AXIS
/*
 * Absolute wrapping axis. The disc position maps onto 0-255 and rolls over
 * at each revolution, which is how real IIDX hardware reports the turntable.
 * Games read the wrap as continuous motion rather than as a jump.
 */
static uint8_t turntable_axis(void)
{
    int32_t p = g_tt_pos % TT_COUNTS_PER_REV;
    if (p < 0) p += TT_COUNTS_PER_REV;
    return (uint8_t)((p * 256) / TT_COUNTS_PER_REV);
}
#endif

static void gamepad_task(void)
{
    if (!tud_hid_n_ready(ITF_GAMEPAD)) return;

    const uint32_t state = g_key_state;

    gamepad_report_t report;
    report.buttons = (uint16_t)(state & ((1u << NUM_BTN) - 1u));

#if TT_GAMEPAD_STYLE == TT_AXIS
    report.x = turntable_axis();
#else
    if (state & (1u << KEY_CW_BIT))  report.buttons |= (1u << (NUM_BTN + 0));
    if (state & (1u << KEY_CCW_BIT)) report.buttons |= (1u << (NUM_BTN + 1));
    report.x = 128;
#endif

    /*
     * Y is present but fixed at centre. Some DirectInput drivers refuse to
     * enumerate a controller that declares only one axis, so the cost of a
     * spare byte buys broad compatibility.
     */
    report.y = 128;

    static gamepad_report_t prev;
    static bool             primed = false;

    if (primed && memcmp(&report, &prev, sizeof(report)) == 0) return;

    if (tud_hid_n_report(ITF_GAMEPAD, 0, &report, sizeof(report))) {
        memcpy(&prev, &report, sizeof(report));
        primed = true;
    }
}

#endif /* BITCON_HAS_GAMEPAD */

/* =============================================================
 *  TinyUSB callbacks
 * ============================================================= */

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer;   (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    /*
     * Host-driven output, e.g. keyboard lock LEDs. Ignored for now.
     * Game-controlled button lighting would hook in here.
     */
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer;   (void)bufsize;
}

/* =============================================================
 *  main
 * ============================================================= */

int main(void)
{
    gpio_setup();

    tud_init(BOARD_TUD_RHPORT);

    multicore_launch_core1(core1_main);

    while (true) {
        tud_task();

#if BITCON_HAS_KEYBOARD
        keyboard_task();
#endif
#if BITCON_HAS_GAMEPAD
        gamepad_task();
#endif
    }
}
