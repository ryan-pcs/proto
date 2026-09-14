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

**Phase: hardware bring-up, mostly finished — now blocked on hardware.**
Every part of the receiver side has now been proven on the real board. What
stops progress is no longer software: there is only one working ESP32 left, and
a CSI link needs two. Data collection has deliberately not started.

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
- **The readings have never been seen on the S3.** CSI starts, but with no
  transmitter nothing has ever come through the callback. Whether a row comes
  out with the same number of values as on the classic chip is still unknown,
  and it is the last question standing between here and collecting data.
- The screen program and the receiver firmware are **now one program** for the
  capture side — see Code layout — but the screen still runs on invented
  numbers. Joining the two properly is staged work that has only just begun.

---

## Hardware, as of now

| Part | State |
|---|---|
| ESP32-S3 N16R8 | Working, and now the receiver. Carries the display. On a YwRobot expansion board with its own DC power jack. |
| Classic ESP32-WROOM (receiver) | **Destroyed** — reverse polarity. |
| Classic ESP32-WROOM (transmitter) | **Destroyed** — burnt out later. Nothing is left to transmit with. |
| 4.0" ST7796S display, 480×320, resistive touch | Working on the S3. A second identical unit is also on hand. |
| Laser distance sensor (I2C, address 0x26) | **Working.** Answers the firmware at start-up on pins 17/18 of the S3. |
| Onboard card reader (on the S3 bench board itself) | **Working.** Mounts, writes, reads back, and creates the capture folders. Pins CLK 39, CMD 38, D0 40. |
| microSD slot on the back of the display board | Wires soldered. Never successfully read. Plan parked in favour of the onboard reader above — see Open questions. |
| Three indicator lights | Wired previously, not reconnected. |

**Only one working ESP32 remains.** A second board — any ESP32 — is the single
thing needed before a reading of any kind can be taken. The transmitter builds
for both the classic chip and the S3, so either will do; whichever is chosen
then has to stay fixed for the whole dataset, because what the receiver measures
depends on the radio at both ends.

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

---

## Open questions

**Where does a second board come from?** Everything else waits on this. Any
ESP32 will do as the transmitter; it uses no pins beyond power.

**Do the readings come out of the S3 in the same shape?** The firmware builds
and CSI starts, but no reading has ever arrived, because there has been nothing
to receive from. On the classic chip a row carries 128 or 256 values in 25
columns. If the S3 differs, the Python tools will still read it — they never
assume a length — but the two chips' data could not be mixed, and the choice
would need recording before any collecting starts.

**Are the S3's readings good enough?** Separate from their shape. Only a real
run against a real transmitter can say.

**Readings are not interchangeable between chips.** What the receiver measures
depends on the hardware at both ends. Whatever combination is chosen must stay
fixed for the whole dataset.

**One column name appears twice.** The CSV header has `sig_mode` at both column
6 and column 22 — the firmware and `collect_burst.py` agree on this, so it is
consistent, but older capture files in `data/` say `rx_format` at column 22
instead. Nothing reads that column today, so nothing is broken. It matters when
the board starts writing files itself: a duplicate name means Python quietly
keeps only the second one. Decide before the card starts recording.

---

## Next steps, in order

1. **Get a second ESP32.** Nothing below can start without one. The transmitter
   builds for either chip (`pio run -e esp32dev` or `-e esp32s3`) and needs no
   wiring beyond power.
2. **Answer the shape question.** With the transmitter running, flash the
   receiver with `CSI_STAGE_A=1` in `display/platformio.ini` and take one
   capture with the laptop's `collect_burst.py`, exactly as before — the merged
   firmware answers the same serial commands. Then check what came back:

   ```
   python -c "import csv,json; r=list(csv.DictReader(open('run.csv'))); print('cols',len(r[0]),'lens',sorted({len(json.loads(x['data'])) for x in r}))"
   ```

   `cols 25` and `lens [128, 256]` means the S3 matches the classic chip and
   the rest of the plan holds. Anything else is the answer to the open question
   above, and needs writing down here before going further.
3. Write captures to the card instead of the cable, with the metadata
   `dataset_report.py` demands.
4. Have the receiver command the transmitter over WiFi, so no laptop is needed.
5. Let the screen drive real runs instead of invented numbers.
6. Reconnect the three indicator lights — no code change needed.
7. Build the physical rig completely, then collect the dataset.

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
