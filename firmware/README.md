# BitCon firmware — RP2350

Rhythm game controller: 9 illuminated buttons plus a photointerrupter
turntable, on a Raspberry Pi Pico 2. Runs as a USB HID keyboard, gamepad,
or both at once.

## Layout

| Path | Purpose |
|---|---|
| `include/config.h` | Output mode, pin map, keymap, tuning. **The only file you should need to edit.** |
| `src/main.c` | Core split, input scan, debounce, quadrature decode, report generation |
| `src/usb_descriptors.c` | HID descriptors for all three modes |
| `include/tusb_config.h` | TinyUSB config, sized from `config.h` |
| `test_logic.c` | Host-side verification, no hardware needed |

## Build

Requires pico-sdk 2.x for RP2350 support.

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .

mkdir build && cd build
cmake ..
make -j
```

Hold BOOTSEL while plugging in USB, then copy `build/bitcon.uf2` to the
drive that appears.

To pick a mode without editing `config.h`:

```bash
cmake -DBITCON_OUTPUT_MODE=GAMEPAD ..
```

## Test

```bash
gcc -O2 -Wall -Wextra -o test_logic test_logic.c && ./test_logic
```

Covers quadrature decoding, turntable event generation, axis wrapping,
debounce across clock rollover, and both report formats. Useful for
sanity-checking parameter changes before you reflash.

## Output modes

Set `OUTPUT_MODE` in `config.h`.

| Mode | What the host sees | Turntable |
|---|---|---|
| `OUT_KEYBOARD` | NKRO keyboard | Two direction keys |
| `OUT_GAMEPAD` | 16-button gamepad, X/Y axes | Axis or two buttons |
| `OUT_COMPOSITE` | Both, as independent interfaces | Both representations at once |

Composite is the default. Two HID interfaces cost one extra endpoint and
let you switch between a keyboard-driven game and a gamepad-driven one
without reflashing.

### Turntable representation

`TT_GAMEPAD_STYLE` controls how the turntable appears in gamepad mode.

`TT_AXIS` (default) maps disc position onto an absolute 8-bit axis that
wraps every revolution. This is what real IIDX hardware does, and games
read the wrap as continuous motion rather than a jump. Scale it with
`TT_COUNTS_PER_REV`, which should be 4 × the slot count on your encoder
disc — quadrature yields four counts per slot.

`TT_DIGITAL` instead emits gamepad buttons 10 and 11, matching keyboard
behaviour. Use it if a game refuses to bind an axis.

Keyboard mode always uses the digital representation. A keyboard has no
axes.

## Design notes

### Core split

`core1` runs a free-running input loop, uninterrupted by the USB stack, so
the scan interval stays even. `core0` runs TinyUSB and builds reports.

They share two 32-bit words. RP2350 SRAM is uncached and aligned 32-bit
accesses are atomic, so no lock is needed. The words are sampled
independently, meaning buttons and turntable position can be one core1
iteration apart — sub-microsecond, and irrelevant here.

### Why no PIO

Two photointerrupters form one quadrature channel pair. A 48-slot disc spun
at 10 rev/s produces roughly 2,000 edges per second, while the `core1` loop
runs in the megahertz. That is hundreds of samples per edge, so nothing can
be missed and the code stays easy to verify.

Move to a PIO decoder if you repurpose `core1`, or switch to a
high-resolution encoder (600 ppr or more).

### Debounce

Eager debounce: a state change applies **immediately**, then further changes
are locked out for `DEBOUNCE_US`. Unlike "N consecutive equal samples
confirms it", press latency is zero.

Do not add an RC filter in hardware. Its time constant becomes input lag.

### 64-bit timestamps

`time_us_32()` wraps every 71.6 minutes, and the signed-difference idiom
`(int32_t)(now - deadline) >= 0` only holds while both values are within
2³¹ of each other. A button that has never been pressed keeps `lock_until`
at 0, so 35.8 minutes after boot the comparison flips and that button goes
dead. The turntable hold fails the opposite way — the condition becomes
true and the key sticks on.

`time_us_64()` overflows in about 584,000 years and is multicore-safe, which
removes the whole class of bug rather than patching one case of it. The
regression is pinned in `test_logic.c`.

### NKRO

The boot keyboard protocol caps at six simultaneous keys. Nine buttons plus
both scratch directions exceeds that, so the keyboard interface uses a
128-bit bitmap report instead — one bit per keycode, no rollover limit.

### Per-mode PIDs

Windows caches HID descriptors against VID/PID. Reflashing a different mode
under the same PID leaves the stale descriptor in place, producing symptoms
that look like firmware bugs. Each mode therefore reports its own PID.

## Tuning

| Parameter | Default | Adjust when |
|---|---|---|
| `DEBOUNCE_US` | 1000 | Chatter appears — raise toward 2000–4000 |
| `TT_STEP` | 2 | Scratch too twitchy (raise) or too sluggish (lower) |
| `TT_HOLD_US` | 40000 | Continuous scratch breaks up (raise) or feels laggy (lower) |
| `TT_COUNTS_PER_REV` | 192 | Must equal 4 × your disc's slot count |

All of these accept `-D` overrides from the build system, so you can sweep
values from a script without touching the source.

## Extension points

**Game-controlled lighting** — hook `tud_hid_set_report_cb()` and have
`core1` read LED state from a shared variable instead of computing it from
button state.

**PS/2 output** — implement it in PIO and select the mode with a button
combination held at boot. GP18, GP19 and GP22 are free.

**More buttons** — the gamepad descriptor already declares 16. Extend
`BTN_PIN_LIST`, `LED_PIN_LIST` and `KEYMAP_LIST` together, and bump
`NUM_BTN`. GP18, GP19, GP22 and GP26–28 are unused, though GP26–28 are the ADC pins
and are not 5 V tolerant.
