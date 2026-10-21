# BitCon firmware — RP2350

Rhythm game controller: 9 illuminated buttons plus a photointerrupter
turntable, on a Raspberry Pi Pico 2. Runs as a USB HID keyboard, gamepad,
or both at once, plus a serial port for the
[configurator page](../configurator/index.html).

## Layout

| Path | Purpose |
|---|---|
| `config.h` | Default output mode, pin map, keymap, tuning. **The only file you should need to edit.** |
| `main.c` | Core split, input scan, debounce, quadrature decode, report generation, configurator port |
| `settings.c` | Stored output mode, kept in the last flash sector |
| `usb_descriptors.c` | USB descriptors for all three modes, each with the serial port |
| `tusb_config.h` | TinyUSB config, sized for composite mode |
| `test_logic.c` | Host-side verification, no hardware needed |

## Build

Requires pico-sdk 2.x for RP2350 support, CMake, and the Arm GNU
toolchain. On macOS:

```bash
brew install cmake picotool
brew install --cask gcc-arm-embedded   # the formula lacks newlib
git clone https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
git -C ~/pico-sdk submodule update --init
```

Then, from this directory:

```bash
export PICO_SDK_PATH=~/pico-sdk
make build
```

`make build` copies `${PICO_SDK_PATH}/external/pico_sdk_import.cmake` here
on first run, configures, and builds `build/bitcon.uf2`. `make clean`
removes the build directory.

Hold BOOTSEL while plugging in USB, then copy `build/bitcon.uf2` to the
drive that appears.

To change the mode a fresh board boots in without editing `config.h`:

```bash
make build MODE=KEYBOARD
```

Once a board is running, use the configurator's "Restart into BOOTSEL"
button (or send `bootsel`) instead of opening the case to reach BOOTSEL.

## Test

```bash
gcc -O2 -Wall -Wextra -o test_logic test_logic.c && ./test_logic
```

Covers quadrature decoding and invalid-step counting, turntable events, axis wrapping,
debounce across clock rollover, and both report formats. Useful for
sanity-checking parameter changes before you reflash.

## Output modes

Change the mode from the configurator page. The board stores it in flash
and reboots so the host re-reads the USB layout; the stored mode survives
unplugging and firmware updates. `DEFAULT_OUTPUT_MODE` in `config.h` (or
`make build MODE=...`) only decides the mode before anything is stored.

| Mode | What the host sees | Turntable |
|---|---|---|
| `OUT_KEYBOARD` | NKRO keyboard | Two direction keys |
| `OUT_GAMEPAD` | 16-button gamepad, X/Y axes | Axis or two buttons |
| `OUT_COMPOSITE` | Both, as independent interfaces | Both representations at once |

Gamepad is the default. Every mode also exposes a CDC serial port, the
configurator port.

## Configurator port

A line protocol over the CDC port, one command per line:

| Command | Reply |
|---|---|
| `info` | `info fw=… mode=… default=… buttons=9 cpr=192 serial=…` |
| `mode` | `mode gamepad` |
| `mode keyboard\|gamepad\|composite` | `ok mode …`, plus ` reboot` when it changed |
| `stream on\|off` | `ok stream on\|off` |
| `state` | one `state` line |
| `identify` | `ok identify`, then every LED blinks for 2 s |
| `bootsel` | `ok bootsel`, then reboots into the UF2 bootloader |

While streaming, the board sends a `state` line whenever input changes (at
most 100 Hz, at least every 500 ms):

```
state keys=201 raw=001 sen=2 tt=-37 err=0
```

`keys` is the debounced key bitmap (bits 0–8 buttons, 9 CW, 10 CCW), `raw`
the button pins before debounce, `sen` the two sensor levels (bit 0 =
SEN1), `tt` the quadrature position, and `err` how many quadrature
transitions were dropped as invalid since boot. Closing the port stops the
stream. Any serial terminal works too, e.g. `screen /dev/cu.usbmodem* 115200`.

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

They share a handful of 32-bit words. RP2350 SRAM is uncached and aligned 32-bit
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

Windows caches HID descriptors against VID/PID. Switching mode under the
same PID leaves the stale descriptor in place, producing symptoms that look
like firmware bugs. Each mode therefore reports its own PID, and
`bcdDevice` is bumped whenever the interface layout changes.

The browser treats each PID as a separate device too, so the configurator
asks you to pick the port once more the first time you use a new mode.

### Several controllers

Each board reports its RP2350 unique ID as the USB serial number, so the
host gives every controller its own serial port and each board keeps its
own stored mode. The configurator talks to one controller per tab. After a
mode change it reconnects only to the controller with the same serial
number, so a second controller, or another tab, cannot take its place.
`identify` (the "Blink LEDs" button) shows which one a tab is connected to.

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
