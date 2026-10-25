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
make build MODE=LR2    # KEYBOARD | LR2 | BEATORAJA | INFINITAS | COMPOSITE
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
| `OUT_BEATORAJA` (default) | 16-button gamepad, X/Y axes | Digital X axis: full scale while spinning, centre when stopped |
| `OUT_LR2` | Same gamepad | Buttons 10 (CW) and 11 (CCW) |
| `OUT_INFINITAS` | Official INFINITAS controller (experimental): its USB ID, button layout and report | Analog X axis: disc position, wrapping every 256 units |
| `OUT_KEYBOARD` | NKRO keyboard | Q / W keys, digital |
| `OUT_COMPOSITE` | Keyboard and gamepad, as independent interfaces | Q / W on the keyboard, analog X axis on the gamepad |

The three gamepad modes exist because games read the turntable in
incompatible ways; see [Turntable modes](#turntable-modes). Every mode also
exposes a CDC serial port, the configurator port.

## Configurator port

A line protocol over the CDC port, one command per line:

| Command | Reply |
|---|---|
| `info` | `info fw=… mode=… default=… buttons=9 cpr=192 serial=…` |
| `mode` | `mode beatoraja` |
| `mode keyboard\|lr2\|beatoraja\|infinitas\|composite` | `ok mode …`, plus ` reboot` when it changed |
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

### Turntable modes

Games read a turntable in one of two ways, and a controller that only
speaks one of them breaks in the others:

- **Analog**: an axis holding the disc position, 0–255, wrapping each way.
  The official INFINITAS controller reports this on X with Y centred.
- **Digital**: three states, neutral, clockwise and counter-clockwise.

**LR2** only reads a digital turntable. LR2 mode puts it on buttons 10 and
11. A direction registers after `TT_DIGITAL_STEP` counts (3.75°) and holds
for `TT_LR2_SUSTAIN_US` (200 ms, arcin's LR2 setting) after the last step,
so slow scratches do not flicker into extra hits. A reversal switches
straight to the other button; both are never held at once.

**beatoraja** with analog scratch off (its default) reads an axis as a
pressed scratch only past ±0.9 of full scale. An analog position axis
therefore "presses" the scratch depending on where the disc happens to
stop, which is the bug the earlier single gamepad mode had. beatoraja
mode drives X to full scale while spinning (255 clockwise, 0
counter-clockwise) and back to centre when the disc stops, using the same
digital logic with a 100 ms hold (`TT_BEATORAJA_SUSTAIN_US`, DJ DAO's
default trigger window) so backspin scratch ends release sooner.

**INFINITAS** reads the analog position. IIDX keeps a history of the
axis and registers a scratch only when it moved at least 2 units against
its value about two frames (~33 ms) earlier. INFINITAS mode therefore moves
the axis `TT_ANALOG_UNITS_PER_COUNT` = 2 units per encoder count, so every
count reaches that threshold and slow scratches register. Because that
also makes a single count enough, a disc resting on a slot edge could
flicker ±1 count into phantom scratches; the axis follows a reversal only
after `TT_ANALOG_REVERSE_COUNTS` of travel. Use this mode for beatoraja too
if you turn analog scratch on.

The INFINITAS numbers come from community reverse engineering of the
arcade game, not from Konami. See [INFINITAS mode](#infinitas-mode-experimental)
for how the controller presents itself to the game.

Keyboard mode uses the digital logic with the LR2 hold. A keyboard has no
axes.

### INFINITAS mode (experimental)

INFINITAS recognises Konami's official controller by its USB VID/PID and
then applies its own fixed key layout, ignoring its key config screen.
INFINITAS mode copies what arcin does to be recognised:

- **USB ID**: Konami's VID `0x1CCF` / PID `0x8048`
  (`INFINITAS_OFFICIAL_ID`). These are Konami's IDs, used only so the game
  talks to the controller. Set it to 0 to keep BitCon's own ID (`0xB173`);
  whether INFINITAS accepts a generic controller is untested.
- **Report**: report ID 1, 15 buttons and a padding bit, then X and Y.
  Keys 1–7 sit on buttons 1–7 and E1–E4 on buttons 9–12.
- **Button layout** (`INFINITAS_BUTTON_LIST`): assumes the top plate's
  seven zig-zag buttons (2–8) are keys 1–7 and the two small end buttons
  (1 and 9) are E1 and E2. E3 and E4 are unassigned. Edit the list if your
  buttons sit differently.
- **Turntable**: the analog axis above. `TT_ANALOG_REVERSE` flips its
  direction if song-select scrolling runs backwards; scratch notes do not
  care.

Button debounce needs no change: arcin recommends none for INFINITAS
because its debounce adds delay, and BitCon's eager debounce adds none.

The configurator still finds the controller in this mode; the browser asks
for permission once, as for any new USB identity.

**Testing it**

1. Set the mode to GAMEPAD · INFINITAS in the configurator. The controller
   restarts as the official controller.
2. In INFINITAS's settings, press "デフォルトに戻す" (reset to default)
   once. Do not rebind keys there.
3. Check keys 1–7, E1 and E2, then the turntable: slow and fast scratches,
   back-and-forth, and a few seconds resting on a slot edge (no phantom
   scratches). Check song-select scroll direction.
4. Repeat at 60 Hz and 120 Hz.

If slow scratches drop, raise `TT_ANALOG_UNITS_PER_COUNT` to 3. If scratches
fire with the disc at rest, raise `TT_ANALOG_REVERSE_COUNTS`. If INFINITAS
does not react at all, check that Windows lists the device with VID `1CCF`
and PID `8048`, and that no real official controller is plugged in.

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

Windows caches HID descriptors against VID/PID. Switching layout under the
same PID leaves the stale descriptor in place, producing symptoms that look
like firmware bugs. Each layout therefore reports its own PID, and
`bcdDevice` is bumped whenever the interface layout changes. LR2 and
beatoraja share one report descriptor, so they share a PID; INFINITAS has
its own report layout and ID.

The browser treats each PID as a separate device too, so the configurator
asks you to pick the port once more the first time you use a new identity.
Switching between LR2 and beatoraja keeps the permission.

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
| `TT_DIGITAL_STEP` | 2 | Digital scratch too twitchy (raise) or too sluggish (lower); 2 is the minimum |
| `TT_LR2_SUSTAIN_US` | 200000 | LR2 / keyboard: scratch breaks up when slow (raise) or releases late (lower) |
| `TT_BEATORAJA_SUSTAIN_US` | 100000 | Same, for beatoraja mode |
| `TT_ANALOG_UNITS_PER_COUNT` | 2 | INFINITAS: slow scratches dropped (raise) or menus scroll too fast (lower) |
| `TT_ANALOG_REVERSE_COUNTS` | 2 | INFINITAS: phantom scratches at rest (raise) or reversals feel late (lower) |
| `TT_ANALOG_REVERSE` | 0 | INFINITAS: song select scrolls backwards (set 1) |
| `INFINITAS_OFFICIAL_ID` | 1 | INFINITAS: report BitCon's own ID instead of Konami's (set 0) |
| `INFINITAS_BUTTON_LIST` | E1, keys 1–7, E2 | INFINITAS: your buttons map to different keys |
| `TT_COUNTS_PER_REV` | 192 | 4 × your disc's slot count; only the configurator uses it |

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
