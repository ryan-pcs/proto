# Handoff — 2026-09-15

*Written at the end of a working session, for whoever picks this up next.*

**`docs/START-HERE.md` is the project's memory and is authoritative for
anything durable** — what the project is, what has been decided, what has been
learned the hard way. Read it first.

This file is only two things: what a new conversation needs that START-HERE
does not yet say, and where things stood when this session ended. **A good
first task is folding the "What START-HERE does not know yet" section below
into START-HERE and deleting this file.** Two documents claiming to describe
the same state is how they start disagreeing.

---

## The one thing blocking everything

**There is no transmitter.** Both classic ESP32-WROOM boards are dead — one to
reverse polarity, one burnt out later. The single working ESP32-S3 is the
receiver and carries the screen.

A CSI link needs two radios: one sending, one measuring. One board cannot
measure itself. **No reading of any kind can be taken until a second board
exists.** Any ESP32 will do — the transmitter builds for both chips and uses no
pins beyond power.

Everything below is either already done or waiting on that.

---

## What is on the board right now

| | |
|---|---|
| Flashed build | `build-28-tally` |
| Port | COM9 — native USB, `VID:PID 303A:1001` |
| `CSI_STAGE_A` | **1** in `display/platformio.ini` (capture compiled in) |
| Gate settings in flash | trigger 200 mm, re-arm 350 mm, hold 1000 ms, scan 3000 ms |

Those gate settings were **set by hand on the SETUP screen**, not the defaults
in `theme.h` (300/450/600/2500). They survived a reflash, which is how the
settings screen and its storage were proven.

Confirmed working on real hardware: CSI start-up, the memory card, the distance
sensor, touch, the sensor-triggered gate flow, and the tally.

---

## What this session did

```
12282cd  Test the data pipeline end to end, and score captures 0-100
0d1daa2  Keep a running tally of scans on the card
6dae55c  Show three verdict levels instead of two
659635b  Add gate scanning UI, settings & defaults
21f0ad3  Enable S3 CSI capture and stage-A receiver
```

Working tree is clean. Both build profiles are green.

---

## What START-HERE does not know yet

Fold these in. They are all durable facts, not session notes.

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

**The project now has one test: `tool/pipeline_selftest.py`.** It builds
synthetic captures in the real CSV format and drives the actual tools —
filter, features, dataset check, train, evaluate — plus both leakage guards.
Run it with `python pipeline_selftest.py` from `tool/`. A pass means the
machinery will carry real data. It says **nothing** about whether CSI can
detect metal; the difference between its two categories is invented.

**The trainer now scores captures 0-100** (`metal_score()`), because the gate's
middle level needs a number, not a label. Model schema is now version 3 and
records the thresholds. The score is a distance ratio between two class
centres, **not a probability** — nearest-centroid has no notion of likelihood.

**The thresholds in `theme.h` are miscalibrated, and it is already known.**
`GATE_CONF_UNSURE = 40` and `GATE_CONF_METAL = 75` were chosen by intuition. A
distance ratio clusters near 50 and does not spread to the extremes the way a
probability does. On the synthetic data, classification was perfect and *no*
metal run reached 75 — the gate would have shown orange for everything and
never turned red. The trainer now prints a NOTE when that happens. **Do not
tune these against synthetic data**; the first real captures should set them.

**Training and test data cannot share an object name.** `train_classifier.py`
aborts if a `(category, object_name)` pair appears in both splits. This is a
good guard — it is what prevents inflated accuracy — but it means **distinct
objects per split must be planned before collection starts**, not sorted out
afterwards.

---

## Open questions, in the order they have to be answered

1. **Where does a second ESP32 come from?** Everything waits here.
2. **Do readings come out of the S3 in the same shape?** CSI starts cleanly
   (four `ESP_OK`s) but no reading has ever arrived, because nothing has
   transmitted. On the classic chip a row is 25 columns with 128 or 256 values.
3. **Are the S3's readings good enough?** Separate from their shape.
4. **Where do the two thresholds belong?** Needs a trained detector tested
   against bags whose contents are known.

---

## The staged plan

Stages A and the gate UI are done. What remains:

| Stage | | State |
|---|---|---|
| A | CSI capture into `display/`, serial sink | **done**, unverified against a transmitter |
| B | Write captures to the card with the metadata `dataset_report.py` demands | not started |
| C | Receiver commands the transmitter over UDP, so no laptop is needed | not started |
| D | Screen drives real runs instead of invented numbers | not started |
| E | `tool/import_card.py` — card files to a trainable dataset | not started |

**Stage A is not finished until the shape question is answered.** Do not build
on it before then: if the shape is wrong the receiver moves back to a classic
ESP32, and `storage.cpp`'s onboard card reader does not exist on that board.

Stage E exists because the device writes `processing_status: "not_run"` and
`dataset_report.py` requires `"success"`. Something has to bridge that, the way
`collect_burst.py` does today.

---

## Working in this repo — things that cost time here

**The shell mangles escape sequences.** A C file written through a shell
command this session had its `'\r'`, `'\n'` and `'\0'` turned into real control
characters, putting an actual zero byte in the source. The write reported
success and the file looked nearly right. Write code through a script file, and
check for stray control bytes, not just that the file changed.

**PlatformIO does not always rebuild when a build flag changes.** Setting a
`-D` through the environment produced a byte-identical firmware — the flag never
reached the compiler. If a flag is meant to change something, check the flash
size moved. Turning CSI capture on moves it from ~473 KB to ~858 KB.

**The terminal is PowerShell, not bash.** No `sed`, no `grep`. `pio` and `git`
are the same either way.

**Bump `BUILD_TAG` in `theme.h` on every change.** If the tag on the screen does
not match what was just flashed, the upload did not take — check that before
debugging anything else.

**Close the serial monitor before uploading**, and name the port
(`--upload-port COM9`) whenever two boards are attached.

**Both profiles must stay green.** `pio run -e esp32s3` and `pio run -e esp32dev`.

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
pio device monitor -p COM9 -b 115200
```

Once a transmitter exists, this is the question Stage A was built to answer —
run one capture with `collect_burst.py` as before, then:

```bash
python -c "import csv,json; r=list(csv.DictReader(open('run.csv'))); print('cols',len(r[0]),'lens',sorted({len(json.loads(x['data'])) for x in r}))"
```

`cols 25` and `lens [128, 256]` means the S3 matches the classic chip. Anything
else is the answer to open question 2 and belongs in START-HERE before any
further work.
