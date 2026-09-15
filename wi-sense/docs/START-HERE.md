# Start here

*Last updated 2026-09-15. Read this first. It says where the project stands,
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

**Phase: hardware bring-up finished. Blocked on one software bug.**
Both ends of the link now exist and work: a replacement transmitter arrived on
2026-09-15, and real CSI has been captured off the S3 for the first time. The
shape question is answered — it matches the classic chip.

What stops collection now is a bug in `tool/collect_burst.py`, which throws away
the CSV header and then rejects every row as invalid, reporting zero samples
while good data flows past it. Details in "Proven on the board (2026-09-15)".
Data collection has still not started.

### Working

- Transmitter and receiver firmware (the student's original work): the
  transmitter creates its WiFi network and sends packets; the receiver captures
  readings and streams them to a laptop.
- The Python tools: cleaning, feature extraction, dataset readiness checking,
  and a guarded baseline trainer.
- A touchscreen interface — six screens — drawing correctly on the ESP32-S3.
- The transmitter firmware also builds for the S3 now.
- **The receiver firmware builds and runs on the ESP32-S3**, and CSI starts up
  there cleanly — see the next section.
- **The memory card, the distance sensor and touch** — all three confirmed on
  the real board, not just written.

### Proven on the board (2026-09-14)

One upload answered several questions at once. From the serial monitor:

```
PROMISCUOUS,result=ESP_OK
CSI_CONFIG,result=ESP_OK
CSI_CALLBACK,result=ESP_OK
CSI_ENABLE,result=ESP_OK
CSI ready
DIAG  build-24-sensor  ...  touch calibrated   card mounted   sensor ready
```

- **CSI starts up on the ESP32-S3.** All four calls return `ESP_OK` and the
  callback registers. The S3 accepts the same CSI settings the classic chip
  used. This is the API half of the old open question, and it is now closed.
- **The memory card works.** `card mounted` in that line is only printed when
  `storageBegin()` *and* `storageSelfTest()` both passed — so the card mounted,
  wrote a file, read it back correctly, and created the capture folders. The
  onboard reader (CLK 39, CMD 38, D0 40) is confirmed in use, not just wired.
- **The distance sensor answers.** `sensorBegin()` succeeded over I2C.
- **Touch is calibrated.** A saved calibration loads at start-up, which can only
  exist if the targets were tapped on the real panel.

These three had been "code written, never run" since they were added. They are
not that any more.

### Proven on the board (2026-09-15) — the first real CSI

A replacement transmitter arrived and was flashed, and **CSI readings came out
of the S3 for the first time.** Open question 2 is closed; the shape matches.

- **The transmitter is a classic ESP32-WROOM** — ESP32-D0WD-V3 rev 3.1, MAC
  `3c:8a:1f:5e:c8:f8`, on a CP2102 bridge. Built from `transmitter/` with
  profile `esp32dev`, and it comes up as `CSI_TX` on channel 11.
- **The receiver re-associates on its own.** It was left running from before
  the transmitter existed, and joined within seconds of the AP appearing —
  `RECEIVER_STATUS,ready=1,wifi=1`. No reflash was needed.
- **The shape matches.** 249 clean rows: 25 columns, 256 values each, header
  byte-identical to the classic-chip format. Details under Open questions.
- **Serial holds up.** 249 of 249 rows intact at 921600 baud when read
  continuously.

**But `collect_burst.py` cannot yet record any of it.** That same run reported
`Receiver CSI samples: 0` and `capture_status: no_csi_received` while 144 good
rows sat in its own `receiver_log`, tagged `INVALID_CSI_DATA`.

The cause is in `tool/collect_burst.py`, not the firmware. The firmware writes
the CSV header inside `csiCaptureStart()`, which runs *before* it prints
`CSI_CAPTURE_READY` — confirmed by reading the port raw, where the header is
line 1 and `CSI_CAPTURE_READY` is line 2. But `arm_receiver()` reads lines
looking only for `CSI_CAPTURE_READY` and **discards every other line it passes**,
then calls `reset_input_buffer()`. The header is thrown away. `collect()` then
starts with `header = None`, and `valid_csi_fields()` returns `False` for every
row when the header is `None` — so all of it is rejected as invalid.

**Nothing can be collected until that is fixed**, and the failure is silent and
misleading: it looks exactly like "no transmitter" or "no CSI", which is the one
symptom this project has learned to read as a hardware fault. Fix `arm_receiver`
to feed the lines it reads through `consume_receiver_lines` (or to keep the
header) rather than dropping them.

### Not working or not built

- No detector has been trained. **No controlled metal or non-metal data has
  been collected at all** — only connection tests.
- **There is no transmitter.** Both classic ESP32 boards are dead. The single
  working S3 is the receiver, and one radio cannot measure itself, so no
  reading of any kind can be taken until a second board exists. This is the
  one thing blocking everything else.
- The three indicator lights still need reconnecting physically. No code
  change is needed — `lampTest()`/`setLights()` have driven them since before
  any of this.
- ~~**The readings have never been seen on the S3.**~~ **Answered 2026-09-15 —
  the readings arrive, and their shape matches.** See "Proven on the board
  (2026-09-15)" below.
- The screen program and the receiver firmware are **now one program** for the
  capture side — see Code layout — but the screen still runs on invented
  numbers. Joining the two properly is staged work that has only just begun.

---

## Hardware, as of now

| Part | State |
|---|---|
| ESP32-S3 N16R8 | Working, and now the receiver. Carries the display. On a YwRobot expansion board with its own DC power jack. |
| Classic ESP32-WROOM (receiver) | **Destroyed** — reverse polarity. |
| Classic ESP32-WROOM (transmitter) | **Working — replaced 2026-09-15.** ESP32-D0WD-V3 rev 3.1, MAC `3c:8a:1f:5e:c8:f8`, CP2102 bridge on COM5. Flashed from `transmitter/`, profile `esp32dev`. |
| Classic ESP32-WROOM (old transmitter) | **Destroyed** — burnt out. Replaced by the board above. |
| 4.0" ST7796S display, 480×320, resistive touch | Working on the S3. A second identical unit is also on hand. |
| Laser distance sensor (I2C, address 0x26) | **Working.** Answers the firmware at start-up on pins 17/18 of the S3. |
| Onboard card reader (on the S3 bench board itself) | **Working.** Mounts, writes, reads back, and creates the capture folders. Pins CLK 39, CMD 38, D0 40. |
| microSD slot on the back of the display board | Wires soldered. Never successfully read. Plan parked in favour of the onboard reader above — see Open questions. |
| Three indicator lights | Wired previously, not reconnected. |

**Two working boards again, as of 2026-09-15.** The transmitter is a classic
ESP32-WROOM; the receiver is the S3. **That pairing is now fixed for the whole
dataset** — what the receiver measures depends on the radio at both ends, so
swapping either board invalidates everything collected before the swap.

### What is on the boards right now

| | Receiver | Transmitter |
|---|---|---|
| Board | ESP32-S3 N16R8 | Classic ESP32-WROOM (D0WD-V3 r3.1) |
| Port | COM9, native USB `303A:1001` | COM5, CP2102 `10C4:EA60` |
| MAC | — | `3c:8a:1f:5e:c8:f8` |
| Built from | `display/`, `-e esp32s3` | `transmitter/`, `-e esp32dev` |
| Flashed build | `build-28-tally` | current `transmitter/` source |

`CSI_STAGE_A` is **1** in `display/platformio.ini`, so the capture is compiled
in. Gate settings held in flash: trigger 200 mm, re-arm 350 mm, hold 1000 ms,
scan 3000 ms. Those were **set by hand on the SETUP screen**, not the defaults
in `theme.h` (300/450/600/2500) — someone reading `theme.h` cold would get them
wrong. They survived a reflash, which is how the settings screen and its
storage were proven.

Confirmed working on real hardware: CSI start-up and capture, the memory card,
the distance sensor, touch, the sensor-triggered gate flow, and the tally.

---

## What the software does today

**The gate shows three levels, not two.** `CLEAR` green and steady, `CHECK`
orange and steady, `SEARCH` red and flashing. The middle one exists because a
detector produces a confidence, not a yes or no, and forcing that into two
buckets reports the least certain cases as though they were certain. Only red
moves; the flash is ~1.7/second, deliberately under the three-a-second
photosensitive-epilepsy guidance, and the footer never flashes.

**The machine keeps a tally on the card.** `/stats.txt` holds the totals as
plain `key=value` lines; `/scans.csv` holds one row per scan. `CLEAR COUNT` on
the TALLY screen renames the history to `/scans-old.csv` rather than deleting
it. Every row carries a `detector` column, which reads `stand-in` while nothing
is trained — the counting is real, the verdicts being counted are not.

**The project has one test: `tool/pipeline_selftest.py`.** It builds synthetic
captures in the real CSV format and drives the actual tools — filter, features,
dataset check, train, evaluate — plus both leakage guards. A pass means the
machinery will carry real data. It says **nothing** about whether CSI can
detect metal; the difference between its two categories is invented.

**The trainer scores captures 0-100** (`metal_score()`), because the gate's
middle level needs a number, not a label. Model schema is version 3 and records
the thresholds. The score is a distance ratio between two class centres, **not
a probability** — nearest-centroid has no notion of likelihood.

**The thresholds in `theme.h` are miscalibrated, and it is already known.**
`GATE_CONF_UNSURE = 40` and `GATE_CONF_METAL = 75` were chosen by intuition. A
distance ratio clusters near 50 and does not spread to the extremes the way a
probability does. On the synthetic data, classification was perfect and *no*
metal run reached 75 — the gate would have shown orange for everything and
never turned red. The trainer prints a NOTE when that happens. **Do not tune
these against synthetic data**; the first real captures should set them.

**Training and test data cannot share an object name.** `train_classifier.py`
aborts if a `(category, object_name)` pair appears in both splits. This is a
good guard — it is what prevents inflated accuracy — but it means **distinct
objects per split must be planned before collection starts**, not sorted out
afterwards.

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
- **The S3 is the receiver**, settled 2026-09-14. It is the only working board,
  it carries the screen, and the card reader the storage code uses exists only
  on it.
- **The receiver commands the transmitter over WiFi**, not over a second serial
  cable, because the finished machine has no laptop attached.
- **The card holds the same CSV text the laptop has always written.** Space is
  not the constraint — a 20-second run at 50Hz is under a megabyte — and
  matching the format exactly means the Python tools need no changes.

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

**Worse than a failed write is a mangled one.** A later session wrote C code
through a shell command and the escape sequences were eaten on the way: `'\r'`,
`'\n'` and `'\0'` became real control characters, putting an actual zero byte
in the middle of the source. The write "succeeded" and the file looked almost
right. Reading a file back is not enough on its own — check it for stray
control characters too, and prefer writing through a script file over pasting
code into a shell command.

**PlatformIO does not always rebuild when a build flag changes.** Setting a
`-D` through the environment produced a firmware byte-for-byte identical to the
one before it — the flag never reached the compiler. If a flag is meant to
change something, check the flash size changed too. Turning the CSI capture on
moves it from about 473KB to about 854KB, because the WiFi stack comes with it.

**Build with PlatformIO (`pio`), never the ESP-IDF tools (`idf.py`).** Mixing
them causes confusing failures.

**The serial monitor holds the port.** An upload while it is open fails, often
without anyone noticing. Close it first.

**With two boards connected, always name the port** (`--upload-port COM9`).
PlatformIO picks one on its own and can easily choose wrong.

**The terminal is PowerShell, not bash.** No `sed`, no `grep`. `pio` and `git`
behave the same either way.

**Bump `BUILD_TAG` in `theme.h` on every change.** If the tag on the screen does
not match what was just flashed, the upload did not take — check that before
debugging anything else.

---

## Open questions

**~~Do the readings come out of the S3 in the same shape?~~ ANSWERED
2026-09-15: yes.** 249 rows captured against the new transmitter. 25 columns,
and the header the S3 prints is **byte-identical** to the one
`collect_burst.py` expects and to the current classic-chip files in `data/`.
Value count was 256 on every row. The 13 classic files in `data/` (3285 rows)
span `[128, 256]`, and several of them are 256-only too, so 256 is ordinary and
not a difference — every frame in this run was HT (`sig_mode=1`, `bandwidth=0`),
and 128 belongs to non-HT frames, which the transmitter never sends. **The two
chips' data is the same shape and the plan holds.**

**Are the S3's readings good enough?** Separate from their shape, and still
open. The rows arrive and parse; whether they carry enough signal to tell metal
from non-metal is what the first real dataset has to show.

**Where do the two gate thresholds belong?** `GATE_CONF_UNSURE` and
`GATE_CONF_METAL` in `theme.h` are guesses (see What the software does today).
Setting them needs a trained detector tested against bags whose contents are
known — not synthetic data.

**Readings are not interchangeable between chips.** What the receiver measures
depends on the hardware at both ends. Whatever combination is chosen must stay
fixed for the whole dataset.

**One column name appears twice — and it already bites.** The CSV header has
`sig_mode` at both column 6 and column 22. Measured 2026-09-15: the 13 classic
files in `data/` carry **three different names** at column 22 — `sig_mode`,
`rx_state` and `rx_format` — all otherwise identical 25-column headers. The S3
prints the `sig_mode` variant, matching `collect_burst.py`.

This is no longer only a future problem. **It made the documented verification
command give the wrong answer**: `csv.DictReader` collapses the duplicate to 24
unique keys, so a *correct* file reports `cols 24`. The command in Next steps
now uses `csv.reader` for that reason. Decide on a single name for column 22
before the card starts recording.

---

## Next steps, in order

1. ~~**Get a second ESP32.**~~ **Done 2026-09-15** — a classic ESP32-WROOM,
   flashed from `transmitter/` with `pio run -e esp32dev --target upload
   --upload-port COM5`. This board and the S3 are now the fixed pair.
2. ~~**Answer the shape question.**~~ **Done 2026-09-15 — it matches:**
   `cols 25`, 256 values a row, header byte-identical to the classic chip's.
   See Open questions. The procedure, kept because it is the check to repeat
   after any firmware change that touches the row format:

   ```
   python -c "import csv,json; rows=list(csv.reader(open('run.csv'))); print('cols',len(rows[0]),'lens',sorted({len(json.loads(r[-1])) for r in rows[1:]}))"
   ```

   `cols 25` means the header matches. `lens` should be a subset of
   `[128, 256]`; on 2026-09-15 it was `[256]`, because every frame this
   transmitter sends is HT. Anything outside that set needs writing down here
   before going further.

   **Before this works at all, fix `arm_receiver()` in `collect_burst.py`** —
   as written it discards the header and records nothing.

   It must be `csv.reader`, not `csv.DictReader` — the header carries
   `sig_mode` twice (see Open questions), so `DictReader` collapses it to 24
   unique keys and a **correct** file reports `cols 24`. The one-liner here
   previously did that; corrected 2026-09-15 against a known-good file.
3. Write captures to the card instead of the cable, with the metadata
   `dataset_report.py` demands.
4. Have the receiver command the transmitter over WiFi, so no laptop is needed.
5. Let the screen drive real runs instead of invented numbers.
6. Reconnect the three indicator lights — no code change needed.
7. Build the physical rig completely, then collect the dataset.

## The staged plan

The gate UI is done, and Stage A closed on 2026-09-15.

| Stage | | State |
|---|---|---|
| A | CSI capture into `display/`, serial sink | **done and verified against a transmitter** |
| B | Write captures to the card with the metadata `dataset_report.py` demands | not started |
| C | Receiver commands the transmitter over UDP, so no laptop is needed | not started |
| D | Screen drives real runs instead of invented numbers | not started |
| E | `tool/import_card.py` — card files to a trainable dataset | not started |

Stage A was held open until the shape question was answered, because a wrong
shape would have sent the receiver back to a classic ESP32 — and `storage.cpp`'s
onboard card reader does not exist on that board. The shape matched, so the S3
stays and the card work stands.

Stage E exists because the device writes `processing_status: "not_run"` and
`dataset_report.py` requires `"success"`. Something has to bridge that, the way
`collect_burst.py` does today.

**Stage B is blocked on nothing in the firmware**, but the laptop collection
path is broken — see `arm_receiver()` under Proven on the board (2026-09-15).

---

## Useful commands

Run from the repository root.

```bash
python wi-sense/tool/pipeline_selftest.py
```

```bash
pio run -d wi-sense/display -e esp32s3 --target upload --upload-port COM9
```

```bash
pio run -d wi-sense/transmitter -e esp32dev --target upload --upload-port COM5
```

```bash
pio device monitor -p COM9 -b 115200
```

Re-check the row shape after any firmware change that touches the row format:

```bash
python -c "import csv,json; rows=list(csv.reader(open('run.csv'))); print('cols',len(rows[0]),'lens',sorted({len(json.loads(r[-1])) for r in rows[1:]}))"
```

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
| `receiver/` | The student's CSI capture firmware. **Frozen as the audited reference copy** — the living copy now lives in `display/src/csi_capture.c`. Keep for comparison; do not develop here. |
| `transmitter/` | The student's transmitter. Builds for both chips. |
| `tool/` | Python: collection, filtering, features, dataset checks, training. |
| `display/` | The touchscreen program **and, since 2026-09-14, the CSI capture**. Current build `build-24-sensor`. See below. |
| `screentest/` | A throwaway proof that LovyanGFX drives the panel. Keep as a reference; delete when no longer useful. |
| `docs/` | This file and the others above. |

Two build profiles everywhere: `pio run -e esp32s3` and `pio run -e esp32dev`.
Both must stay green.

Inside `display/src/`, the capture side is:

| File | What it is |
|---|---|
| `csi_capture.c/.h` | The capture, moved in from `receiver/`. Its own header lists every way it differs from the frozen original — read that before changing it. |
| `csi_sink.cpp/.h` | Where a finished reading goes: the serial cable today, the card later. Swapping one for the other is one function call. |

`CSI_STAGE_A` in `display/platformio.ini` switches the capture on. **It is
currently set to 1**, and the board is flashed that way, ready for the moment a
transmitter exists — the only reason nothing comes out is that there is nothing
to receive from. Set it to 0 and the program behaves exactly as it did before
any of this; the screens are unaffected either way. With it on, the board also
answers the laptop's `collect_burst.py` as the old receiver did, which is how
the shape question gets answered.

One fix was needed to make the capture build for the S3: the gain-control block
must stay switched off there, because the four `esp_csi_gain_ctrl_*` functions
it calls do not exist in this SDK. With it off the readings are passed through
unscaled — exactly what the classic ESP32 has always done — so nothing about
the output changes.

---

## Conventions

The screen program carries a version label shown at the bottom of the start-up
and home screens, and printed to the serial monitor every ten seconds. **If
that label does not match the code you just flashed, the upload did not take.**
Check that before debugging anything else.

Pins live in `display/src/board_pins.h`. One place, both chips.

The capture prints through Arduino's `Serial`, not `ets_printf`. On the S3 those
are not the same place — `ets_printf` always goes to UART0 while `Serial`
follows the USB build flags — so everything the program says, readings included,
comes out of one socket. `sdkconfig.defaults` in `receiver/` does nothing under
PlatformIO: it asks for 128 receive buffers and a 30-second watchdog, and the
prebuilt Arduino libraries give 32 and 5 regardless. That has always been true,
on both chips.

The card works, but so far holds only a self-test file, the empty capture
folders, and a plain-text event log — no capture behind the screen's numbers is
real, and nothing is real scan data yet. The run lines it logs are still marked
`(sim)` for exactly that reason, and that marking comes off only when the
numbers behind them are real. The program says what it actually knows rather
than implying more, and that honesty is deliberate — it matches the student's
own rule that nothing may claim metal detection until a model has been trained
and tested.
