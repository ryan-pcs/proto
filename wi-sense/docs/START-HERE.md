# Start here

*Last updated 2026-09-14. Read this first. It says where the project stands,
what has already been decided, what has been learned the hard way, and which
document answers what. Everything referenced lives in this repository, so it
survives whatever conversation or computer it was written in.*

---

## What this is

A student innovation project for a science and technology competition:
detecting concealed metal objects in bags at a school gate using two ESP32
boards and WiFi Channel State Information — the distortion a WiFi signal picks
up travelling from one board to the other. An object between them changes that
distortion; the aim is to tell metal from non-metal by it, without opening bags
or slowing the queue.

---

## Where it stands

**Phase: hardware bring-up.** Proving each component works with the parts
actually in hand. Data collection has deliberately not started.

### Working

- Transmitter and receiver firmware (the student's original work): the
  transmitter creates its WiFi network and sends packets; the receiver captures
  readings and streams them to a laptop.
- The Python tools: cleaning, feature extraction, dataset readiness checking,
  and a guarded baseline trainer.
- A touchscreen interface — six screens — drawing correctly on the ESP32-S3.
- The transmitter firmware also builds for the S3 now.

### Not working or not built

- No detector has been trained. **No controlled metal or non-metal data has
  been collected at all** — only connection tests.
- **Distance sensor: code written, not yet proven.** `sensorBegin()`/
  `sensorRead()` in `src/sensor.cpp` were complete but never called from
  `main.cpp` — the likely reason it "did nothing" when first wired up
  physically, since the firmware never once asked it anything, regardless of
  wiring. Now called at start-up and, once running, a few times a second
  while the home screen is showing, feeding the live distance readout that
  screen already had built in. The three indicator lights needed no code
  changes — `lampTest()`/`setLights()` were already wired in from the start;
  they only need the physical reconnection.
- **Memory card: code written, not yet proven.** The S3 bench board turns out
  to have its own onboard card reader (separate from the back-panel slot on
  the display, and not needing to share the screen's wires) — pins CLK 39,
  CMD 38, D0 40, confirmed against the board rather than guessed.
  `src/storage.cpp` now mounts it, self-tests it, creates the capture
  folders, and appends events to `/log.txt` on the card. Wired into start-up,
  but never built, flashed, or run against a real card. The earlier
  back-panel-slot plan (`docs/sd-card-plan.md`) is parked, not gone — it's
  still the option if the classic ESP32 ends up as the receiver, since that
  board has no onboard reader.
- **Touch: code written, not yet proven.** `display_driver.h` now configures
  the touch chip and `main.cpp` reads real presses against the buttons
  already defined in `screens.cpp`, replacing the old timed auto-advance
  (which still runs as a fallback until a calibration has been saved). A new
  home-screen button, TOUCH SETUP, runs calibration and saves it so it
  survives a power cycle. None of this has been built, flashed, or touched
  on a real board yet — see Next steps.
- The screen program and the receiver firmware are still two separate programs.

---

## Hardware, as of now

| Part | State |
|---|---|
| ESP32-S3 N16R8 | Working. Carries the display. On a YwRobot expansion board with its own DC power jack. |
| Classic ESP32-WROOM | **Destroyed** — reverse polarity. |
| 4.0" ST7796S display, 480×320, resistive touch | Working on the S3. A second identical unit is also on hand. |
| Laser distance sensor (I2C, address 0x26) | Worked on the old board. Wired to pins 17/18 on the S3, tested standalone with a component tester. Firmware now calls it at start-up (previously never did) — not yet confirmed with the firmware actually reading it. |
| Onboard card reader (on the S3 bench board itself) | Wiring confirmed (CLK 39, CMD 38, D0 40). Code written, not yet flashed or tested. |
| microSD slot on the back of the display board | Wires soldered. Never successfully read. Plan parked in favour of the onboard reader above — see Open questions. |
| Three indicator lights | Wired previously, not reconnected. |

The student has the two boards originally intended as transmitter and receiver.

---

## Decisions taken — do not relitigate without reason

- **Two boards, not three.** A transmitter, and a receiver that also carries
  the screen. Reasoning in the display brief.
- **The screen is a collection tool first**, a result display second. It exists
  to make collecting labelled runs faster, not to show verdicts that do not yet
  exist.
- **Time-of-flight sensor only** for detecting a bag, mounted on the receiver's
  side of the tray, looking across. Not infrared, not ultrasonic.
- **No cancel button during a scan**, because the firmware deliberately has no
  cancel path.
- **The red and green lights stay dark** until a detector has genuinely been
  trained and tested. Blue for "ready" is fine now.
- **Build the rig completely before collecting data.** Every physical change
  makes earlier readings unusable.
- **LovyanGFX, not TFT_eSPI** — see below.

---

## Learned the hard way — do not repeat these

**TFT_eSPI cannot drive this panel on the ESP32-S3.** It crashes during
start-up; working around that with `USE_HSPI_PORT` stops the crash but the
screen never shows a picture. LovyanGFX works immediately. A whole day went
into proving this. The panel is described in code in `src/display_driver.h`
rather than through build settings.

**Brownouts were the recurring theme, not a faulty chip.** CSI capture requires
WiFi power-saving to be switched off, so the radio runs flat out — far harsher
than ordinary WiFi. A board that handles normal WiFi fine can collapse under
this firmware. The expansion board, with its own DC jack and reserve
capacitors, solved it. **Do not run this from a laptop USB port.**

**Connectors are a real fault source.** One display went from answering nothing
to answering correctly purely from being unplugged and reconnected. If
something behaves inconsistently, reseat before theorising.

**Pins that must never be used:**

- Classic ESP32: **GPIO 1 and 3** carry the CSI readings out. Also 6–11 (flash),
  0/2/12/15 (boot mode), 34–39 (input only).
- ESP32-S3 N16R8: **GPIO 26–37** are the chip's own flash and memory. They
  appear on the expansion board and look usable. They are not.

**Writes to disk from an AI session can silently fail.** It happened three times
in one day — the tool reported success and the file was unchanged, so builds
kept flashing old code. Always read a file back after writing it. The build
label exists for this reason.

**Build with PlatformIO (`pio`), never the ESP-IDF tools (`idf.py`).** Mixing
them causes confusing failures.

**The serial monitor holds the port.** An upload while it is open fails, often
without anyone noticing. Close it first.

**With two boards connected, always name the port** (`--upload-port COM9`).
PlatformIO picks one on its own and can easily choose wrong.

---

## Open questions

**Which chip is the receiver now?** The plan was WROOM as receiver, S3 as
transmitter. The WROOM is dead and the S3 currently carries the display. Either
the S3 becomes the receiver, or a replacement WROOM does and the display moves
back. This has not been settled.

**Does the receiver firmware work on the S3?** It has never been built for that
chip, and nobody has confirmed the CSI readings come out in the same shape. The
student's entire validated pipeline — coverage rules, row checks, the audit
history — was built and tested on the classic chip. This is the largest
remaining unknown.

**The memory card has never actually been read from yet.** Two attempts at
the back-panel slot failed. `src/storage.cpp` has now been rewritten from
scratch around the S3 board's onboard reader instead (a different, simpler
connection — no wire-sharing with the screen), but this is still unverified
against real hardware. If the receiver ends up being the classic ESP32
instead of the S3 (see the open question above), the onboard reader isn't
available there and the back-panel slot becomes the only option again.

**Readings are not interchangeable between chips.** What the receiver measures
depends on the hardware at both ends. Whatever combination is chosen must stay
fixed for the whole dataset.

---

## Next steps, in order

1. **Test touch, the memory card, and the distance sensor together** — the
   code for all three is now written (current build `build-24-sensor`,
   2026-09-14) but none of it has been through a real build or flash yet.
   One upload and one look at the serial monitor checks all three at once:
   `pio run -e esp32s3 --target upload` then `pio device monitor`. Confirm
   the build label reads `build-24-sensor` first — if it doesn't, the upload
   didn't take and nothing below means anything yet. Then look for:
   - Touch: press TOUCH SETUP on the home screen to calibrate (needed once;
     until then screens fall back to the old timer). Wiring still needed:
     one wire, `T_CS` to IO15 on the S3.
   - Card: `[card] mounted, self-test passed` in the monitor, the boot
     screen's "Card" line going green, and afterward `/log.txt` plus the
     empty `/data/...` folders on the card itself.
   - Sensor: `[sensor] answered, ready` in the monitor, and a live
     millimetre reading on the home screen that changes when you wave a hand
     over it. If this instead reports "no answer on the two wires", that
     points at wiring/power rather than firmware.
   The indicator lights need no firmware change, only reconnecting physically
   — the code has driven them since before this session.
2. Settle which chip is the receiver, and build the receiver firmware for it.
3. Join the screen program and the receiver firmware into one.
4. Build the physical rig completely, then collect the dataset.

---

## Document map

| Document | What it answers |
|---|---|
| `docs/knowledge-base.md` | What the project contains and how the code is organised |
| `docs/wiring-pin-map.md` | Which wire goes where, and why those pins |
| `docs/tft-display-integration-brief.md` | What the screen is for, and the two-board decision |
| `docs/sd-card-plan.md` | How card storage will work, in three stages |
| `display/README.md` | Building, flashing and using the screen program |
| `display/TROUBLESHOOTING.md` | When the screen misbehaves |

The student's own documents are the authority on the sensing side:
`PROJECT_GUIDE.md`, `COMMANDS.md`, `STORAGE.md`, `CHANGELOG.md`.

Note: `docs/wiring-pin-map.md` still describes the classic ESP32 as the main
board. `display/src/board_pins.h` is now the authority for pins on both chips.

---

## Code layout

| Folder | What it is |
|---|---|
| `receiver/` | The student's CSI capture firmware. Classic ESP32 only. Hardened, audited — treat with care. |
| `transmitter/` | The student's transmitter. Builds for both chips. |
| `tool/` | Python: collection, filtering, features, dataset checks, training. |
| `display/` | The touchscreen program. Current build `build-24-sensor`. |
| `screentest/` | A throwaway proof that LovyanGFX drives the panel. Keep as a reference; delete when no longer useful. |
| `docs/` | This file and the others above. |

Two build profiles everywhere: `pio run -e esp32s3` and `pio run -e esp32dev`.

---

## Conventions

The screen program carries a version label shown at the bottom of the start-up
and home screens, and printed to the serial monitor every ten seconds. **If
that label does not match the code you just flashed, the upload did not take.**
Check that before debugging anything else.

Pins live in `display/src/board_pins.h`. One place, both chips.

The card (once proven working) only holds a self-test file, the empty capture
folders, and a plain-text event log so far — no capture behind the screen's
numbers is real, and nothing is real scan data yet. The program says so
rather than implying otherwise, and that honesty is deliberate — it matches
the student's own rule that nothing may claim metal detection until a model
has been trained and tested.
