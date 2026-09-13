# Display prototype

Runs on **one ESP32 with the 4.0" screen attached**. Nothing else is needed — no
second board, no WiFi, no radio work.

It shows the real screens of the finished machine, driven by stand-in numbers,
so the layout and the touch behaviour can be judged and fixed now rather than
later.

## What it deliberately does not do

No WiFi. No CSI capture. No detection. The numbers on the scanning screen are
invented. This is a prototype of the interface, not of the machine.

## Two boards, one program

The bench board (ESP32-S3 N16R8) and the final receiver (classic
ESP32-WROOM-32) do not number their pins the same way. Several of the classic
board's numbers do not exist on the S3 at all, and others are reserved there
for the chip's own memory.

So the program carries **two pin tables** and you pick one at build time. The
code in `src/` is identical either way.

### ESP32-S3 N16R8 — bench board

| Display pin | GPIO |
|---|---|
| SCK, T_CLK | 12 |
| SDI (MOSI), T_DIN | 11 |
| SDO (MISO), T_DO | 13 |
| CS | 10 |
| DC / RS | 14 |
| RESET | 9 |
| LED (backlight) | 8 |
| T_CS | 15 |

| Other | GPIO |
|---|---|
| Green light | 4 |
| Red light | 5 |
| Blue light | 6 |

### Classic ESP32-WROOM-32 — final receiver

Use `../docs/wiring-pin-map.md`. That document is the authority for the real
build.

The indicator lights are optional for this test. Without them attached,
everything else still works.

## Build and flash

On the bench board:

```powershell
Set-Location .\wi-sense\display
pio run -e esp32s3 --target upload
pio device monitor
```

On the receiver board later:

```powershell
pio run -e esp32dev --target upload
```

The S3 has two USB sockets. If the upload is not found, try the other one.

## What you should see

1. **Start-up screen** — four checks ticking to OK one at a time, then it moves
   on by itself after about three seconds.
2. **Home** — two large buttons. The blue indicator light comes on here.
3. `COLLECT` → **label picker** — EMPTY, METAL, NON-METAL. The one you choose
   is filled in; the others are outlined.
4. **Run settings** — duration and rate as buttons, with the destination file
   name shown underneath so you can see exactly what would be saved.
5. `START` → **scanning** — a progress bar and four live numbers. Coverage turns
   green above 75% and red below, which is the threshold that decides whether a
   real run counts.
6. **Result** — PASS or FAIL with the reason.
7. `IDENTIFY` from Home shows `MODEL NOT TRAINED`, which is correct and will
   stay that way until a detector genuinely exists.

## Touch calibration

The panel is resistive, so it needs calibrating once. The first time it runs you
will be asked to touch an arrow in each corner with the stylus. The result is
kept on the chip and survives a power cycle.

**To redo it:** hold a finger on the screen while powering the board on.

## Two things built in on purpose

**No cancel button on the scanning screen.** The firmware deliberately has no
cancel path, so every started run is cleaned up the same way. Adding a cancel
button here would reintroduce exactly what was removed on purpose.

**The red and green lights stay dark.** Blue means ready and is used. Red and
green are reserved for a bag verdict and must not be driven by a guess until a
detector has been trained and tested. The place where they would eventually be
driven is marked in `main.cpp`.

## How the real numbers plug in later

`ui_state.h` is the seam. Everything the screen can show lives in `UiState`.
The drawing code never asks where a number came from.

Today `ui_state.cpp` invents the numbers. Later, on the receiver, the same
fields are filled from real capture counters. **The screens do not change.**

*As an analogy: it is like laying out an exam paper with placeholder questions.
The layout is finished and correct before the real items are encoded, and
swapping the real items in afterwards does not disturb it.*

## If something looks wrong

| Symptom | Likely cause |
|---|---|
| White or blank screen | Check `TFT_CS`, `TFT_DC`, `TFT_RST` wiring, and that the backlight pin is high. |
| Screen works, colours inverted | Add `-DTFT_INVERSION_ON=1` to `build_flags`. |
| Picture sideways or mirrored | Change `tft.setRotation(1)` in `setup()` to 3. |
| Touch presses land in the wrong place | Redo calibration: hold the screen while powering on. |
| Touch does nothing at all | Check `T_CS` on GPIO 4, and that `T_CLK`/`T_DIN`/`T_DO` share pins 18/23/19. |
| Random resets | Power supply. Check the 470 µF capacitor and that the supply is rated 2 A. |
| Board not found when uploading (S3) | Try the other USB socket. Or hold BOOT, tap RESET, release BOOT, then upload. |
| No serial output on the S3 | You are probably plugged into the other socket. Try it, or set `ARDUINO_USB_CDC_ON_BOOT` to 0. |
| Build fails on the graphics library | The library version may not match the platform version. Try `bodmer/TFT_eSPI@^2.5.43` first; if it still fails, report the error rather than guessing. |
