# If the screen does not work

Work through this with the board in front of you. It is ordered by how often
each cause actually turns out to be the problem.

---

## Step 1 — find out whether the board is even running

Open the serial monitor first, before looking at the screen:

```powershell
pio device monitor -e esp32dev
```

Press the RESET button on the ESP32. You should see:

```
[1] board awake
[2] starting the screen
[3] screen started - colours should be showing now
[4] screen test finished
[5] touch ready
[6] ready
```

**Where it stops tells you where the fault is.**

| What you see | What it means |
|---|---|
| Nothing at all | The board is not running, or the monitor is on the wrong port. Not a screen problem. |
| Stops after `[1]` | Wiring is shorting something. Unplug the display and reset — if `[1]` to `[6]` then appear, a wire is at fault. |
| Stops after `[2]` | Rare. Usually MISO is shorted to ground. |
| Reaches `[6]` but the screen is dark | The program is fine. The problem is entirely in the display wiring — go to Step 2. |
| Reaches `[6]` and the screen works | Nothing is wrong. |

This split is the most useful thing in this document. It separates "the board
is broken" from "the screen is wired wrong", and those need completely
different searches.

---

## Step 2 — is the backlight on?

Take the board somewhere dim and look at the panel closely.

**Completely black, no glow at all** — the backlight is not powered. The screen
may be working perfectly and you simply cannot see it. Check:

- `LED` pin connected to **3.3V**
- `VCC` connected, and to the correct voltage for your board
- `GND` connected

**A faint grey or white glow, but nothing drawn** — the backlight is fine and
the controller is not receiving anything. Go to Step 3.

Nine times out of ten a "dead" screen on a first attempt is an unlit backlight.

---

## Step 3 — screen lit but blank white

The controller is powered but not being told anything. In order of likelihood:

1. **`DC`/`RS` and `CS` swapped or loose.** These two matter more than any
   other. `CS` is GPIO 33, `DC`/`RS` is GPIO 25.
2. **`SCK` and `SDI` swapped.** `SCK` is GPIO 18, `SDI` (MOSI) is GPIO 23.
3. **`RESET` not connected.** GPIO 26. Some boards will not start without it.
4. **A shared wire only reaching one destination.** GPIO 18, 23 and 19 each
   carry two wires. Check both ends of all six.

Reseat every jumper. Loose DuPont connectors are extremely common and look
perfectly seated.

---

## Step 4 — something appears, but wrong

| What you see | Fix |
|---|---|
| Colours swapped — red shows as blue | Add `-DTFT_RGB_ORDER=TFT_BGR` to `build_flags` in `platformio.ini` |
| Everything looks like a photo negative | Add `-DTFT_INVERSION_ON=1`. If it is already there, change it to `-DTFT_INVERSION_OFF=1` |
| Picture sideways or upside down | In `main.cpp`, change `tft.setRotation(1)` to `3` |
| Picture mirrored | Try rotation `0` or `2` |
| Only three corner squares, or the picture is cut off | Width and height are wrong for your panel. Check the `TFT_WIDTH` and `TFT_HEIGHT` lines |
| Random speckles, or it works then stops | Go to Step 5 |

After changing anything in `platformio.ini`, rebuild and upload again — those
settings are read when the code is built, not when it runs.

---

## Step 5 — works sometimes, or flickers

This is almost always one of two things.

**Wiring.** The three shared wires carry a fast signal. Long wires, wires
bundled alongside the power leads, or a loose connector all cause exactly this.
Shorten them, separate them from the power wires, reseat everything.

**Speed.** If the wiring is sound, the connection may simply be running faster
than your wires can manage. In `platformio.ini`, find `SPI_FREQUENCY=40000000`
and change it to `27000000`. If that settles it, the wiring was marginal —
worth tidying rather than leaving slow.

---

## Step 6 — touch problems

| What happens | Cause |
|---|---|
| It says "No touch panel found" | `T_CS` (GPIO 4) not connected, or one of the three shared wires is not reaching the touch side |
| Calibration runs but presses land in the wrong place | Redo it: hold a finger on the screen while powering the board on |
| Presses land wildly wrong, calibration does not help | `T_DIN` and `T_DO` swapped. `T_DIN` goes to GPIO 23, `T_DO` to GPIO 19 |
| Touch works but is erratic | Press harder. It is a resistive panel and needs firm contact — use the stylus |

Remember the screen works perfectly well without touch. If touch is fighting
you, leave it disconnected for now: the program detects that and cycles through
the screens on a timer so you can still review the design.

---

## Upload problems (not a screen fault)

| Symptom | Fix |
|---|---|
| `could not open port` | Wrong port, or a serial monitor is already holding it. Close the monitor first. |
| `Failed to connect to ESP32` | Hold the BOOT button while the upload starts, release when it begins writing. |
| Port does not appear at all | The USB-to-serial driver is missing. This board uses a CP2102 — install Silicon Labs' driver. |

---

## When to stop and ask

If Step 1 shows `[6] ready` and you have been through Steps 2 to 4 without
success, stop rather than continuing to swap wires. Send:

- what the serial monitor prints
- what the panel does — dark, white, glowing, speckled
- a photo of the wiring

Those three together usually identify it immediately.
