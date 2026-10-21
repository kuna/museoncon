/*
 * main.c - BitCon firmware for RP2350 (Raspberry Pi Pico 2).
 *
 * 9 illuminated buttons + a 2-channel photointerrupter turntable, exposed
 * as a USB HID keyboard, gamepad, or both, plus a serial port for the
 * configurator page. See config.h.
 *
 * Core split:
 *   core1 - free-running input scan, debounce, quadrature decode, LEDs.
 *   core0 - TinyUSB stack, HID report generation, configurator port.
 *
 * The cores communicate through a handful of 32-bit words. RP2350 SRAM is
 * uncached and aligned 32-bit accesses are atomic, so no lock is needed.
 * The words are sampled independently, so buttons and turntable position
 * can be up to one core1 iteration apart - sub-microsecond, irrelevant here.
 */

#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "pico/unique_id.h"
#include "hardware/gpio.h"
#include "tusb.h"

#include "config.h"
#include "settings.h"
#include "usb_descriptors.h"

#ifndef BITCON_FW_VERSION
#define BITCON_FW_VERSION "dev"
#endif

/* =============================================================
 *  Shared state
 * ============================================================= */

/* Bits 0..NUM_KEYS-1 of the internal key bitmap. Written by core1. */
static volatile uint32_t g_key_state = 0;

/* Accumulated quadrature position. Written by core1. */
static volatile int32_t g_tt_pos = 0;

/* Button pins before debounce, bit i = button i+1. Written by core1. */
static volatile uint32_t g_raw_state = 0;

/* Sensor levels, bit 0 = SEN1 (A), bit 1 = SEN2 (B). Written by core1. */
static volatile uint32_t g_sen_state = 0;

/* Quadrature transitions dropped as invalid since boot. Written by core1. */
static volatile uint32_t g_tt_errors = 0;

/* =============================================================
 *  Quadrature decode table
 * ============================================================= */

/*
 * Index = (previous_state << 2) | current_state, each state being (B << 1) | A.
 *
 * Forward Gray sequence: 00 -> 01 -> 11 -> 10 -> 00.
 * Invalid transitions (both bits changing at once, which means a sample was
 * missed or the signal glitched) decode to 0 and are dropped. They are also
 * counted, so the configurator can show a sensor that is misbehaving.
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
         * Switch input, active low.
         *
         * The internal pull-up is on by default. The board is meant to
         * carry an external 10k, but if it is missing the pin floats and
         * reads as permanently pressed, so the internal one earns its keep
         * as a fail-safe. See USE_INTERNAL_PULLUP_BTN in config.h.
         */
        gpio_init(BTN_PINS[i]);
        gpio_set_dir(BTN_PINS[i], GPIO_IN);
        gpio_set_pulls(BTN_PINS[i], USE_INTERNAL_PULLUP_BTN ? true : false, false);
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

    /*
     * Photointerrupter inputs, active low. The phototransistor is open
     * collector and cannot drive high at all, so a pull-up is mandatory
     * here rather than merely advisable. The internal one is a bring-up
     * crutch; fit the external 4.7k for usable edge rates.
     */
    for (int i = 0; i < 2; i++) {
        const uint8_t pin = PIN_SEN_BASE + i;
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_set_pulls(pin, USE_INTERNAL_PULLUP_SEN ? true : false, false);
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
    uint32_t tt_errors = 0;           /* invalid transitions             */

    uint8_t q_prev = (uint8_t)((gpio_get_all() >> PIN_SEN_BASE) & 0x3u);

    while (true) {
        const uint64_t now_us = time_us_64();
        const uint32_t gpio   = gpio_get_all();

        uint32_t keys     = 0;
        uint32_t raw_bits = 0;
        uint32_t led_val  = 0;

        /* ---- buttons: eager debounce ---- */
        for (int i = 0; i < NUM_BTN; i++) {
            const bool raw = ((gpio >> BTN_PINS[i]) & 1u) == 0u;  /* active low */
            if (raw) raw_bits |= (1u << i);

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
            const int8_t step = QDEC[(q_prev << 2) | q];
            if (step == 0) tt_errors++;  /* both channels changed at once */
            tt_pos += step;
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
        g_raw_state = raw_bits;
        g_sen_state = q;
        g_tt_errors = tt_errors;
    }
}

/* =============================================================
 *  core0 - keyboard reports
 * ============================================================= */

/*
 * NKRO bitmap report: 128 bits, one per keycode. Unlike the 6-key-rollover
 * boot keyboard this places no limit on simultaneous input, so nine buttons
 * plus both scratch directions all register together.
 */
#define KBD_REPORT_BYTES 16

static const uint8_t KEYMAP[NUM_KEYS] = KEYMAP_LIST;

static void keyboard_task(uint8_t instance)
{
    if (!tud_hid_n_ready(instance)) return;

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

    if (tud_hid_n_report(instance, 0, report, sizeof(report))) {
        memcpy(prev, report, sizeof(report));
        primed = true;
    }
}

/* =============================================================
 *  core0 - gamepad reports
 * ============================================================= */

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

static void gamepad_task(uint8_t instance)
{
    if (!tud_hid_n_ready(instance)) return;

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

    if (tud_hid_n_report(instance, 0, &report, sizeof(report))) {
        memcpy(&prev, &report, sizeof(report));
        primed = true;
    }
}

/* =============================================================
 *  core0 - configurator port
 * ============================================================= */

/*
 * Line protocol on the CDC serial port. One command per line, one reply
 * line per command:
 *
 *   info           -> info fw=<ver> mode=<m> default=<m> buttons=9 cpr=192
 *                     serial=<id>
 *   mode           -> mode <m>
 *   mode <m>       -> ok mode <m>          already running in <m>
 *                     ok mode <m> reboot   stored; reboots to re-enumerate
 *   stream on|off  -> ok stream on|off
 *   state          -> state ...            one snapshot
 *   bootsel        -> ok bootsel           reboots into the UF2 bootloader
 *   anything else  -> err <reason>
 *
 * <m> is keyboard, gamepad or composite. While streaming, a state line goes
 * out whenever the input changes, at most every STREAM_MIN_US and at least
 * every STREAM_IDLE_US:
 *
 *   state keys=<hex> raw=<hex> sen=<0-3> tt=<int> err=<uint>
 *
 * keys is the debounced key bitmap (bits 0-8 buttons, 9 CW, 10 CCW), raw the
 * button pins before debounce, sen the sensor levels (bit 0 = SEN1), tt the
 * quadrature position and err the invalid transitions since boot.
 */

#define STREAM_MIN_US    10000   /* 100 Hz cap                         */
#define STREAM_IDLE_US   500000  /* heartbeat while nothing changes    */
#define REBOOT_DELAY_US  100000  /* lets the reply reach the host first */

typedef struct {
    uint32_t keys;
    uint32_t raw;
    uint32_t sen;
    int32_t  tt;
    uint32_t err;
} input_snapshot_t;

static char   g_line[64];
static size_t g_line_len  = 0;
static bool   g_streaming = false;

typedef enum { REBOOT_NONE, REBOOT_SAVE_MODE, REBOOT_BOOTSEL } reboot_t;

static reboot_t g_reboot = REBOOT_NONE;
static uint8_t  g_reboot_mode;
static uint64_t g_reboot_at;

static void port_printf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) {
        tud_cdc_write(buf, (uint32_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
        tud_cdc_write_flush();
    }
}

static input_snapshot_t input_snapshot(void)
{
    return (input_snapshot_t){
        .keys = g_key_state,
        .raw  = g_raw_state,
        .sen  = g_sen_state,
        .tt   = g_tt_pos,
        .err  = g_tt_errors,
    };
}

static int format_state(char *buf, size_t size, const input_snapshot_t *s)
{
    return snprintf(buf, size,
                    "state keys=%03" PRIx32 " raw=%03" PRIx32 " sen=%" PRIu32
                    " tt=%" PRId32 " err=%" PRIu32 "\n",
                    s->keys, s->raw, s->sen, s->tt, s->err);
}

static void schedule_reboot(reboot_t kind, uint8_t mode)
{
    g_reboot      = kind;
    g_reboot_mode = mode;
    g_reboot_at   = time_us_64() + REBOOT_DELAY_US;
}

static void handle_mode(const char *arg)
{
    const uint8_t current = settings_mode();

    if (arg == NULL) {
        port_printf("mode %s\n", settings_mode_name(current));
        return;
    }

    uint8_t mode;
    if (!settings_mode_from_name(arg, &mode)) {
        port_printf("err unknown mode: %s\n", arg);
    } else if (mode == current) {
        port_printf("ok mode %s\n", arg);
    } else {
        port_printf("ok mode %s reboot\n", arg);
        schedule_reboot(REBOOT_SAVE_MODE, mode);
    }
}

static void handle_command(char *line)
{
    const char *cmd = strtok(line, " \t");
    const char *arg = strtok(NULL, " \t");
    if (cmd == NULL) return;

    if (strcmp(cmd, "info") == 0) {
        char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
        pico_get_unique_board_id_string(serial, sizeof(serial));
        port_printf("info fw=%s mode=%s default=%s buttons=%d cpr=%d serial=%s\n",
                    BITCON_FW_VERSION, settings_mode_name(settings_mode()),
                    settings_mode_name(DEFAULT_OUTPUT_MODE), NUM_BTN,
                    TT_COUNTS_PER_REV, serial);
    } else if (strcmp(cmd, "mode") == 0) {
        handle_mode(arg);
    } else if (strcmp(cmd, "stream") == 0) {
        if (arg != NULL && strcmp(arg, "on") == 0) {
            g_streaming = true;
        } else if (arg != NULL && strcmp(arg, "off") == 0) {
            g_streaming = false;
        } else {
            port_printf("err usage: stream on|off\n");
            return;
        }
        port_printf("ok stream %s\n", arg);
    } else if (strcmp(cmd, "state") == 0) {
        char buf[96];
        const input_snapshot_t s = input_snapshot();
        format_state(buf, sizeof(buf), &s);
        port_printf("%s", buf);
    } else if (strcmp(cmd, "bootsel") == 0) {
        port_printf("ok bootsel\n");
        schedule_reboot(REBOOT_BOOTSEL, 0);
    } else {
        port_printf("err unknown command: %s\n", cmd);
    }
}

static void stream_task(void)
{
    static input_snapshot_t last;
    static uint64_t         last_sent = 0;

    if (!g_streaming) return;

    const uint64_t now = time_us_64();
    if (now - last_sent < STREAM_MIN_US) return;

    const input_snapshot_t s = input_snapshot();
    const bool changed = memcmp(&s, &last, sizeof(s)) != 0;
    if (!changed && now - last_sent < STREAM_IDLE_US) return;

    char buf[96];
    const int n = format_state(buf, sizeof(buf), &s);

    /*
     * Drop the frame rather than wait when the host is not keeping up.
     * Core0 also feeds the HID reports, and the next frame carries the
     * same state anyway.
     */
    if (n <= 0 || tud_cdc_write_available() < (uint32_t)n) return;

    tud_cdc_write(buf, (uint32_t)n);
    tud_cdc_write_flush();
    last      = s;
    last_sent = now;
}

static void config_port_task(void)
{
    uint8_t  buf[64];
    uint32_t n;

    while ((n = tud_cdc_read(buf, sizeof(buf))) > 0) {
        for (uint32_t i = 0; i < n; i++) {
            const char c = (char)buf[i];
            if (c == '\r' || c == '\n') {
                g_line[g_line_len] = '\0';
                if (g_line_len > 0) handle_command(g_line);
                g_line_len = 0;
            } else if (g_line_len < sizeof(g_line) - 1) {
                g_line[g_line_len++] = (char)tolower((unsigned char)c);
            }
        }
    }

    stream_task();

    if (g_reboot != REBOOT_NONE && time_us_64() >= g_reboot_at) {
        if (g_reboot == REBOOT_BOOTSEL) reset_usb_boot(0, 0);
        settings_save_and_reboot(g_reboot_mode);
    }
}

/* =============================================================
 *  TinyUSB callbacks
 * ============================================================= */

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)itf; (void)rts;

    /* The host closed the port. Stop streaming into a FIFO nobody reads. */
    if (!dtr) {
        g_streaming = false;
        g_line_len  = 0;
    }
}

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
    settings_load();
    gpio_setup();

    tud_init(BOARD_TUD_RHPORT);

    multicore_launch_core1(core1_main);

    const int keyboard = usb_hid_keyboard_instance();
    const int gamepad  = usb_hid_gamepad_instance();

    while (true) {
        tud_task();

        if (keyboard >= 0) keyboard_task((uint8_t)keyboard);
        if (gamepad  >= 0) gamepad_task((uint8_t)gamepad);

        config_port_task();
    }
}
