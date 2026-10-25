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

#define DEBOUNCE_US        1000
#define TT_STEP            2
#define TT_HOLD_US         40000
#define TT_COUNTS_PER_REV  (4 * 48)
#define NUM_BTN            9

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

static void test_turntable_events(void)
{
    printf("turntable direction events\n");

    int32_t  pos = 0, ref = 0;
    uint64_t hold[2] = { 0, 0 };
    uint64_t t = 100000;
    int cw = 0, ccw = 0;

    for (int i = 0; i < 12; i++) {
        pos += 1;
        int32_t d = pos - ref;
        if (d >= TT_STEP)       { ref += TT_STEP; hold[0] = t + TT_HOLD_US; cw++;  }
        else if (d <= -TT_STEP) { ref -= TT_STEP; hold[1] = t + TT_HOLD_US; ccw++; }
        t += 500;
    }
    CHECK(cw == 6,  "12 forward counts emit 6 CW events at TT_STEP=2");
    CHECK(ccw == 0, "forward motion emits no CCW events");

    CHECK(hold[0] > t, "key is held while the hold window is open");
    t += TT_HOLD_US + 1000;
    CHECK(!(hold[0] > t), "key releases after the hold window expires");

    /* Hysteresis: alternating +-1 must never cross the threshold. */
    pos = 0; ref = 0; cw = 0; ccw = 0;
    for (int i = 0; i < 20; i++) {
        pos += (i % 2) ? -1 : +1;
        int32_t d = pos - ref;
        if (d >= TT_STEP)       { ref += TT_STEP; cw++;  }
        else if (d <= -TT_STEP) { ref -= TT_STEP; ccw++; }
    }
    CHECK(cw == 0 && ccw == 0, "+-1 jitter emits no events");
}

/* ============================================================= */

static uint8_t axis_from(int32_t tt_pos)
{
    int32_t p = tt_pos % TT_COUNTS_PER_REV;
    if (p < 0) p += TT_COUNTS_PER_REV;
    return (uint8_t)((p * 256) / TT_COUNTS_PER_REV);
}

static void test_turntable_axis(void)
{
    printf("turntable axis mapping\n");

    CHECK(axis_from(0) == 0, "origin maps to 0");
    CHECK(axis_from(TT_COUNTS_PER_REV) == 0, "one revolution wraps to 0");
    CHECK(axis_from(TT_COUNTS_PER_REV / 2) == 128, "half revolution maps to 128");
    CHECK(axis_from(TT_COUNTS_PER_REV * 7) == 0, "many revolutions still wrap to 0");

    /* Negative positions must wrap into range, not clamp or go wild. */
    CHECK(axis_from(-1) == axis_from(TT_COUNTS_PER_REV - 1),
          "negative position wraps to the top of the range");
    CHECK(axis_from(-TT_COUNTS_PER_REV) == 0, "one reverse revolution wraps to 0");
    CHECK(axis_from(-TT_COUNTS_PER_REV / 2) == 128,
          "half reverse revolution maps to 128");

    /* Output must stay inside 0-255 for a wide sweep of inputs. */
    for (int32_t p = -100000; p <= 100000; p += 37) {
        uint8_t a = axis_from(p);
        (void)a;  /* uint8_t cannot exceed 255; the real check is no crash/UB */
    }

    /* Monotonic within one revolution. */
    bool monotonic = true;
    int prev = -1;
    for (int32_t p = 0; p < TT_COUNTS_PER_REV; p++) {
        int a = axis_from(p);
        if (a < prev) { monotonic = false; break; }
        prev = a;
    }
    CHECK(monotonic, "axis rises monotonically across one revolution");

    /* Forward motion across the wrap point must step by 1, not jump. */
    int before = axis_from(TT_COUNTS_PER_REV - 1);
    int after  = axis_from(TT_COUNTS_PER_REV);
    CHECK(before == 255 || before == 254, "position just before wrap is near 255");
    CHECK(after == 0, "position at wrap returns to 0");
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
    uint64_t hold = now + TT_HOLD_US;
    CHECK(hold > now, "an open hold reads as held");
    CHECK(!(hold > now + TT_HOLD_US + 1), "the hold releases once past its deadline");
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

static void test_gamepad_report(void)
{
    printf("gamepad report packing\n");

    CHECK(sizeof(gamepad_report_t) == 4, "report is exactly 4 bytes");

    uint32_t state = (1u << 0) | (1u << 8);  /* buttons 1 and 9 */

    gamepad_report_t r;
    r.buttons = (uint16_t)(state & ((1u << NUM_BTN) - 1u));
    r.x = 128;
    r.y = axis_from(TT_COUNTS_PER_REV / 4);

    CHECK((r.buttons & (1u << 0)) != 0, "button 1 set");
    CHECK((r.buttons & (1u << 8)) != 0, "button 9 set");
    CHECK((r.buttons & (1u << 4)) == 0, "button 5 clear");
    CHECK(r.y == 64, "quarter revolution maps to 64 on Y");
    CHECK(r.x == 128, "X stays centred");

    /* Button field must not leak the turntable bits in axis mode. */
    state = 0xFFFFFFFFu;
    uint16_t masked = (uint16_t)(state & ((1u << NUM_BTN) - 1u));
    CHECK(masked == 0x1FF, "only the nine button bits survive masking");

    /* Digital turntable style adds buttons 10 and 11. */
    state = (1u << 9) | (1u << 10);
    uint16_t btns = (uint16_t)(state & ((1u << NUM_BTN) - 1u));
    if (state & (1u << 9))  btns |= (1u << (NUM_BTN + 0));
    if (state & (1u << 10)) btns |= (1u << (NUM_BTN + 1));
    CHECK(btns == ((1u << 9) | (1u << 10)),
          "digital turntable lands on gamepad buttons 10 and 11");
}

/* ============================================================= */

int main(void)
{
    test_quadrature();
    test_turntable_events();
    test_turntable_axis();
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
