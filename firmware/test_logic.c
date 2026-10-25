/*
 * test_logic.c - host-side verification of the pure logic in main.c.
 *
 * Nothing here touches hardware, so it runs anywhere:
 *   gcc -O2 -Wall -Wextra -o test_logic test_logic.c && ./test_logic
 *
 * The algorithms are duplicated rather than included because main.c pulls in
 * pico-sdk headers. Keep the two in sync when you change either.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define DEBOUNCE_US                1000
#define TT_DIGITAL_STEP            2
#define TT_LR2_SUSTAIN_US          200000
#define TT_BEATORAJA_SUSTAIN_US    100000
#define TT_ANALOG_UNITS_PER_COUNT  2
#define TT_ANALOG_REVERSE_COUNTS   2
#define NUM_BTN                    9
#define KEY_CW_BIT                 (NUM_BTN + 0)
#define KEY_CCW_BIT                (NUM_BTN + 1)

#define OUT_INFINITAS  2
#define OUT_COMPOSITE  3
#define OUT_LR2        4
#define OUT_BEATORAJA  5

/* INFINITAS layout from config.h: BitCon button i drives bit INF[i]. */
static const uint8_t INFINITAS_BITS[NUM_BTN] = { 8, 0, 1, 2, 3, 4, 5, 6, 9 };

static int failures = 0;

#define CHECK(cond, msg)                                   \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("  FAIL  %s\n", msg);                   \
            failures++;                                    \
        }                                                  \
    } while (0)

static const int8_t QDEC[16] = {
     0, +1, -1,  0,
    -1,  0,  0, +1,
    +1,  0,  0, -1,
     0, -1, +1,  0,
};

/* ============================================================= */

static int32_t run_quad_counting(const uint8_t *seq, int n, uint32_t *errors)
{
    int32_t pos = 0;
    uint8_t prev = seq[0];
    *errors = 0;
    for (int i = 1; i < n; i++) {
        if (seq[i] != prev) {
            const int8_t step = QDEC[(prev << 2) | seq[i]];
            if (step == 0) (*errors)++;
            pos += step;
            prev = seq[i];
        }
    }
    return pos;
}

static int32_t run_quad(const uint8_t *seq, int n)
{
    uint32_t errors;
    return run_quad_counting(seq, n, &errors);
}

static void test_quadrature(void)
{
    printf("quadrature decode\n");

    const uint8_t fwd[] = { 0, 1, 3, 2, 0 };
    CHECK(run_quad(fwd, 5) == 4, "one forward cycle is +4");

    const uint8_t rev[] = { 0, 2, 3, 1, 0 };
    CHECK(run_quad(rev, 5) == -4, "one reverse cycle is -4");

    const uint8_t fwd3[] = { 0,1,3,2,0,1,3,2,0,1,3,2,0 };
    CHECK(run_quad(fwd3, 13) == 12, "three forward cycles are +12");

    const uint8_t jitter[] = { 0, 1, 0, 1, 0, 1, 0 };
    CHECK(run_quad(jitter, 7) == 0, "boundary jitter nets to zero");

    const uint8_t bad[] = { 0, 3, 0 };
    CHECK(run_quad(bad, 3) == 0, "invalid transitions are dropped");

    /* 0->1:+1 1->3:+1 3->0:invalid 0->1:+1 1->3:+1 3->2:+1 = +5 */
    const uint8_t mixed[] = { 0, 1, 3, 0, 1, 3, 2 };
    CHECK(run_quad(mixed, 7) == 5, "decoding recovers after an invalid step");

    uint32_t errors;
    run_quad_counting(mixed, 7, &errors);
    CHECK(errors == 1, "an invalid step is counted once");
    run_quad_counting(fwd3, 13, &errors);
    CHECK(errors == 0, "clean rotation counts no errors");
    const uint8_t held[] = { 0, 0, 0, 1, 1, 1 };
    run_quad_counting(held, 6, &errors);
    CHECK(errors == 0, "an unchanged sample is not an error");

    const uint8_t gray[4] = { 0, 1, 3, 2 };
    for (int i = 0; i < 4; i++) {
        uint8_t a = gray[i], b = gray[(i + 1) % 4];
        CHECK(QDEC[(a << 2) | b] == +1, "gray forward step is +1");
        CHECK(QDEC[(b << 2) | a] == -1, "gray reverse step is -1");
    }

    for (int s = 0; s < 4; s++) {
        CHECK(QDEC[(s << 2) | s] == 0, "unchanged state contributes zero");
    }
}

/* ============================================================= */

/* Mirrors tt_digital_update() in main.c. */
typedef struct {
    uint64_t sustain_us;
    int32_t  ref;
    uint64_t ref_at;
    uint64_t release_at;
    int8_t   dir;
} tt_digital_t;

static int8_t tt_digital_update(tt_digital_t *d, int32_t pos, uint64_t now_us)
{
    const int32_t delta = pos - d->ref;

    if (delta >= TT_DIGITAL_STEP || delta <= -TT_DIGITAL_STEP) {
        d->dir        = (delta > 0) ? 1 : -1;
        d->ref        = pos;
        d->ref_at     = now_us;
        d->release_at = now_us + d->sustain_us;
    } else if (now_us - d->ref_at >= d->sustain_us) {
        d->ref    = pos;
        d->ref_at = now_us;
    }

    if (now_us >= d->release_at) d->dir = 0;
    return d->dir;
}

static void test_turntable_digital(void)
{
    printf("digital turntable (LR2, beatoraja, keyboard)\n");

    const uint64_t sustains[2] = { TT_LR2_SUSTAIN_US, TT_BEATORAJA_SUSTAIN_US };

    for (int m = 0; m < 2; m++) {
        const uint64_t sustain = sustains[m];
        tt_digital_t d = { .sustain_us = sustain };
        uint64_t t = 5000000;  /* 5 s after boot */
        int32_t pos = 0;

        /* The firmware polls continuously, so it has anchored at rest. */
        tt_digital_update(&d, pos, t);

        /* Spin forward one count every 5 ms (about 1 rev/s). */
        int first_cw = -1, saw_ccw = 0, gaps = 0;
        for (int i = 1; i <= 100; i++) {
            pos++; t += 5000;
            int8_t dir = tt_digital_update(&d, pos, t);
            if (dir > 0 && first_cw < 0) first_cw = i;
            if (dir < 0) saw_ccw = 1;
            if (first_cw > 0 && dir == 0) gaps++;
        }
        CHECK(first_cw == TT_DIGITAL_STEP, "clockwise registers after one step of travel");
        CHECK(!saw_ccw, "forward spin never reads as counter-clockwise");
        CHECK(gaps == 0, "a steady spin holds without flicker");

        /* Stop: held until one sustain window after the last step, which
         * came at most one step (10 ms here) before the disc stopped. */
        uint64_t stopped = t;
        int8_t dir = tt_digital_update(&d, pos, stopped + sustain - 10000);
        CHECK(dir > 0, "direction is still held just inside the sustain window");
        dir = tt_digital_update(&d, pos, stopped + sustain + 1000);
        CHECK(dir == 0, "direction releases once the sustain window passes");

        /* Spin again, then reverse: switches straight over, never both. */
        t = stopped + sustain + 2000;
        for (int i = 0; i < 10; i++) { pos++; t += 5000; tt_digital_update(&d, pos, t); }
        int steps_to_flip = 0;
        for (int i = 1; i <= 10; i++) {
            pos--; t += 5000;
            if (tt_digital_update(&d, pos, t) < 0) { steps_to_flip = i; break; }
        }
        CHECK(steps_to_flip == TT_DIGITAL_STEP, "a reversal flips direction after one step back");

        /* Slot-edge jitter at rest never registers. */
        tt_digital_t j = { .sustain_us = sustain };
        int jitter_fired = 0;
        t += 1000000;
        for (int i = 0; i < 1000; i++) {
            t += 1000;
            if (tt_digital_update(&j, (i % 2) ? 1 : 0, t) != 0) jitter_fired = 1;
        }
        CHECK(!jitter_fired, "+-1 count jitter never registers");

        /* Creep slower than one step per window never registers. */
        tt_digital_t c = { .sustain_us = sustain };
        int crept = 0;
        int32_t cpos = 0;
        for (int i = 0; i < 50; i++) {
            cpos++; t += sustain + sustain / 2;
            if (tt_digital_update(&c, cpos, t) != 0) crept = 1;
        }
        CHECK(!crept, "creep below one step per window never registers");
    }

    /* Long uptime: timestamps far past 2^32 microseconds still work. */
    tt_digital_t d = { .sustain_us = TT_LR2_SUSTAIN_US };
    uint64_t t = 900000000000ULL;
    tt_digital_update(&d, 0, t);
    t += 5000; tt_digital_update(&d, 1, t);
    t += 5000;
    CHECK(tt_digital_update(&d, 2, t) > 0, "registers after about 10 days of uptime");
}

/* ============================================================= */

/* Mirrors tt_analog_update() in main.c. */
typedef struct {
    int32_t pos;
    int8_t  dir;
} tt_analog_t;

static void tt_analog_update(tt_analog_t *a, int32_t pos)
{
    const int32_t delta = pos - a->pos;
    if (delta == 0) return;

    const int8_t dir = (delta > 0) ? 1 : -1;
    if (dir == a->dir || delta >= TT_ANALOG_REVERSE_COUNTS ||
        delta <= -TT_ANALOG_REVERSE_COUNTS) {
        a->pos = pos;
        a->dir = dir;
    }
}

static uint8_t analog_axis(int32_t pos)
{
    return (uint8_t)(pos * TT_ANALOG_UNITS_PER_COUNT);
}

static void test_turntable_analog(void)
{
    printf("analog turntable (INFINITAS, composite)\n");

    tt_analog_t a = { 0 };

    /* Forward motion is followed count for count once moving. */
    tt_analog_update(&a, 2);
    CHECK(a.pos == 2, "the first move of two counts is followed");
    int every_count_moves_two = 1;
    for (int32_t p = 3; p < 40; p++) {
        uint8_t before = analog_axis(a.pos);
        tt_analog_update(&a, p);
        if ((uint8_t)(analog_axis(a.pos) - before) != 2) every_count_moves_two = 0;
    }
    CHECK(every_count_moves_two,
          "every count moves the axis 2 units, IIDX's minimum scratch step");

    /* Resting on a slot edge: +-1 jitter leaves the axis alone. */
    int32_t rest = a.pos;
    int moved = 0;
    for (int i = 0; i < 100; i++) {
        tt_analog_update(&a, rest - 1);
        if (a.pos != rest) moved = 1;
        tt_analog_update(&a, rest);
    }
    CHECK(!moved, "+-1 count jitter against the direction never moves the axis");

    /* A real reversal is followed after two counts back. */
    tt_analog_update(&a, rest - 1);
    CHECK(a.pos == rest, "one count back is held");
    tt_analog_update(&a, rest - 2);
    CHECK(a.pos == rest - 2 && a.dir < 0, "two counts back follows the reversal");

    /* Wrapping: modulo 256 in both directions. */
    CHECK(analog_axis(0) == 0, "origin is 0");
    CHECK(analog_axis(127) == 254, "127 counts is 254 units");
    CHECK(analog_axis(128) == 0, "128 counts wraps to 0");
    CHECK(analog_axis(-1) == 254, "one count below origin wraps to 254");
    CHECK(analog_axis(-128) == 0, "128 counts below origin wraps to 0");
    CHECK((uint8_t)(analog_axis(-1) - analog_axis(0)) == 254,
          "the wrap reads as -2 units in 8-bit arithmetic, not a jump");
}

/* ============================================================= */

static void test_debounce(void)
{
    printf("eager debounce with a 64-bit clock\n");

    bool     state = false;
    uint64_t lock  = 0;
    uint64_t t     = 50000;

    bool raw = true;
    if (raw != state && t >= lock) { state = raw; lock = t + DEBOUNCE_US; }
    CHECK(state == true, "press applies immediately, zero added latency");

    t += 100; raw = false;
    if (raw != state && t >= lock) { state = raw; lock = t + DEBOUNCE_US; }
    CHECK(state == true, "chatter inside the lockout is ignored");

    t += 300; raw = true;
    if (raw != state && t >= lock) { state = raw; lock = t + DEBOUNCE_US; }
    CHECK(state == true, "re-bounce inside the lockout is ignored");

    t += DEBOUNCE_US; raw = false;
    if (raw != state && t >= lock) { state = raw; lock = t + DEBOUNCE_US; }
    CHECK(state == false, "genuine release after the lockout is applied");

    /*
     * These are the points where the old 32-bit signed-difference version
     * broke: a button never pressed keeps lock at 0, and once the clock
     * passes 2^31 the comparison flipped and the button went dead.
     */
    const uint64_t marks[] = {
        2200000000ULL,    /* past 2^31, about 36 minutes  */
        4300000000ULL,    /* past 2^32, about 72 minutes  */
        900000000000ULL,  /* about 10 days                */
    };

    for (int i = 0; i < 3; i++) {
        bool st = false;
        uint64_t lk = 0, now = marks[i];
        CHECK((true != st && now >= lk) == true,
              "an untouched button still registers after a long uptime");
    }

    for (int i = 0; i < 3; i++) {
        uint64_t hold = 0;
        CHECK(!(hold > marks[i]), "an expired hold never reads as held");
    }

    uint64_t now  = marks[1];
    uint64_t hold = now + TT_LR2_SUSTAIN_US;
    CHECK(hold > now, "an open hold reads as held");
    CHECK(!(hold > now + TT_LR2_SUSTAIN_US + 1), "the hold releases once past its deadline");
}

/* ============================================================= */

static void test_keyboard_report(void)
{
    printf("NKRO keyboard report packing\n");

    const uint8_t keymap[11] = {
        0x04, 0x16, 0x07, 0x09, 0x2C, 0x0D, 0x0E, 0x0F, 0x33, 0x14, 0x1A
    };

    uint32_t state = (1u << 0) | (1u << 4) | (1u << 9);  /* A, Space, Q */

    uint8_t report[16] = { 0 };
    for (int i = 0; i < 11; i++) {
        if (state & (1u << i)) {
            uint8_t kc = keymap[i];
            report[kc >> 3] |= (uint8_t)(1u << (kc & 7u));
        }
    }

    CHECK((report[0x04 >> 3] & (1u << (0x04 & 7))) != 0, "A bit set");
    CHECK((report[0x2C >> 3] & (1u << (0x2C & 7))) != 0, "Space bit set");
    CHECK((report[0x14 >> 3] & (1u << (0x14 & 7))) != 0, "Q bit set");
    CHECK((report[0x16 >> 3] & (1u << (0x16 & 7))) == 0, "S bit clear");

    int set = 0;
    for (int i = 0; i < 16; i++)
        for (int b = 0; b < 8; b++)
            if (report[i] & (1u << b)) set++;
    CHECK(set == 3, "exactly three bits set");

    /* All eleven at once - the reason for NKRO over 6KRO. */
    uint8_t full[16] = { 0 };
    for (int i = 0; i < 11; i++) {
        uint8_t kc = keymap[i];
        full[kc >> 3] |= (uint8_t)(1u << (kc & 7u));
    }
    set = 0;
    for (int i = 0; i < 16; i++)
        for (int b = 0; b < 8; b++)
            if (full[i] & (1u << b)) set++;
    CHECK(set == 11, "eleven simultaneous keys pack to eleven bits");
}

/* ============================================================= */

typedef struct __attribute__((packed)) {
    uint16_t buttons;
    uint8_t  x;
    uint8_t  y;
} gamepad_report_t;

/* Mirrors infinitas_buttons() and analog_axis() in main.c. */
static uint16_t infinitas_buttons(uint32_t state)
{
    uint16_t out = 0;
    for (int i = 0; i < NUM_BTN; i++) {
        if (state & (1u << i)) out |= (uint16_t)(1u << INFINITAS_BITS[i]);
    }
    return out;
}

static uint8_t reversible_axis(int32_t pos, int reverse)
{
    return analog_axis(reverse ? -pos : pos);
}

/* Mirrors gamepad_task() in main.c. */
static gamepad_report_t make_gamepad(uint8_t mode, uint32_t state, int32_t axis_pos)
{
    const int cw  = (state & (1u << KEY_CW_BIT))  != 0;
    const int ccw = (state & (1u << KEY_CCW_BIT)) != 0;

    gamepad_report_t r = {
        .buttons = (uint16_t)(state & ((1u << NUM_BTN) - 1u)),
        .x = 128,
        .y = 128,
    };

    switch (mode) {
    case OUT_LR2:
        if (cw)  r.buttons |= (1u << (NUM_BTN + 0));
        if (ccw) r.buttons |= (1u << (NUM_BTN + 1));
        break;
    case OUT_BEATORAJA:
        if (cw)  r.x = 255;
        if (ccw) r.x = 0;
        break;
    case OUT_INFINITAS:
        r.buttons = infinitas_buttons(state);
        r.x = analog_axis(axis_pos);
        break;
    default:
        r.x = analog_axis(axis_pos);
        break;
    }
    return r;
}

static void test_gamepad_report(void)
{
    printf("gamepad report packing, per mode\n");

    CHECK(sizeof(gamepad_report_t) == 4, "report is exactly 4 bytes");

    const uint32_t btns = (1u << 0) | (1u << 8);  /* buttons 1 and 9 */
    const uint32_t cw   = 1u << KEY_CW_BIT;
    const uint32_t ccw  = 1u << KEY_CCW_BIT;
    const uint8_t modes[3] = { OUT_LR2, OUT_BEATORAJA, OUT_INFINITAS };

    for (int m = 0; m < 3; m++) {
        gamepad_report_t r = make_gamepad(modes[m], btns | cw, 0);
        if (modes[m] != OUT_INFINITAS) {
            CHECK((r.buttons & 0x1FF) == btns, "buttons 1-9 map straight through");
        }
        CHECK(r.y == 128, "Y stays centred");
    }

    gamepad_report_t r = make_gamepad(OUT_LR2, cw, 50);
    CHECK(r.buttons == (1u << 9) && r.x == 128, "LR2: clockwise is button 10, axes centred");
    r = make_gamepad(OUT_LR2, ccw, 50);
    CHECK(r.buttons == (1u << 10), "LR2: counter-clockwise is button 11");

    r = make_gamepad(OUT_BEATORAJA, cw, 50);
    CHECK(r.x == 255 && r.buttons == 0, "beatoraja: clockwise is X full scale, no buttons");
    r = make_gamepad(OUT_BEATORAJA, ccw, 50);
    CHECK(r.x == 0, "beatoraja: counter-clockwise is X minimum");
    r = make_gamepad(OUT_BEATORAJA, 0, 50);
    CHECK(r.x == 128, "beatoraja: idle is centred");
    /* beatoraja normalises 0..255 to -1..1 and needs |value| > 0.9. */
    CHECK((255 / 127.5 - 1.0) > 0.9 && (0 / 127.5 - 1.0) < -0.9,
          "beatoraja: full scale clears the 0.9 threshold both ways");
    CHECK((128 / 127.5 - 1.0) < 0.9, "beatoraja: centre stays below the threshold");

    r = make_gamepad(OUT_INFINITAS, cw, 50);
    CHECK(r.x == 100 && r.buttons == 0, "INFINITAS: X is the position, no turntable buttons");
    r = make_gamepad(OUT_INFINITAS, 0, -1);
    CHECK(r.x == 254, "INFINITAS: negative positions wrap");

    /* Official layout: keys 1-7 on bits 0-6, E1/E2 on bits 8/9. */
    uint16_t keys = 0;
    for (int i = 1; i <= 7; i++) keys |= infinitas_buttons(1u << i);
    CHECK(keys == 0x7F, "INFINITAS: buttons 2-8 become keys 1-7");
    CHECK(infinitas_buttons(1u << 0) == (1u << 8), "INFINITAS: button 1 is E1");
    CHECK(infinitas_buttons(1u << 8) == (1u << 9), "INFINITAS: button 9 is E2");
    CHECK((infinitas_buttons(0x1FF) & ~0x37Fu) == 0,
          "INFINITAS: button 8 and bits 10-15 stay clear");
    CHECK((infinitas_buttons(0xFFFFFFFFu) & (1u << 15)) == 0,
          "INFINITAS: the padding bit is never set");

    /* Reversal flips direction, keeping the 2-units-per-count step. */
    CHECK(reversible_axis(1, 1) == 254 && reversible_axis(1, 0) == 2,
          "INFINITAS: reversing turns +1 count into -2 units");
    CHECK((uint8_t)(reversible_axis(10, 1) - reversible_axis(9, 1)) == 254,
          "INFINITAS: reversed axis still moves 2 units per count");

    r = make_gamepad(OUT_COMPOSITE, btns, 50);
    CHECK(r.buttons == btns && r.x == 100, "composite: plain buttons, analog X");
}

/* ============================================================= */

int main(void)
{
    test_quadrature();
    test_turntable_digital();
    test_turntable_analog();
    test_debounce();
    test_keyboard_report();
    test_gamepad_report();

    if (failures == 0) {
        printf("\nall checks passed\n");
        return 0;
    }
    printf("\n%d check(s) failed\n", failures);
    return 1;
}
