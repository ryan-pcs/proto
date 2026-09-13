# 4.0" TFT Display — Integration Brief

*Written 2026-09-11. A heads-up document: what this screen can do for Wi-Sense, what the SD slot is good for, which pins to use, and what the on-screen interface could look like. Nothing here is built yet.*

---

## 1. What you actually have

From the spec sheet on your board:

| Item | Value |
|---|---|
| Size | 4.0 inch |
| Resolution | 480 × 320 pixels |
| Driver chip | ST7796S |
| Connection | 4-wire SPI |
| Touch | Yes (resistive — it ships with a stylus) |
| Visible screen area | 83.5 × 55.7 mm |
| Board size | 108 × 61.7 mm |
| Weight | 53 g |
| Card slot | microSD, on the back, own labeled pads |

Two practical notes from the photos:

- The touch is **resistive**, not the glass-capacitive kind on a phone. It works with a fingernail or the stylus, and it needs a firm press. Design consequence: **big buttons**. Nothing smaller than about 60 × 60 pixels.
- The microSD pads on the back (`SD_CS`, `SD_MOSI`, `SD_MISO`, `SD_SCK`) are brought out **separately** from the display pins. That means you choose whether the card shares wiring with the screen or gets its own — you are not locked in.

---

## 2. What the screen can do for this project

Split into what helps *now* and what helps *later*, because these are very different.

### Useful now — while you are still collecting data

Right now your bottleneck is that no controlled `metal` / `non_metal` data exists, and collecting it means sitting at a laptop terminal typing labels for every single run. You need dozens of runs, all consistently labeled. That is exactly where a touchscreen earns its place.

1. **Label picker.** Big buttons: EMPTY / METAL / NON-METAL. One press instead of typing a folder name and an object name. Fewer typos means fewer runs thrown away.
2. **Live coverage readout during a run.** Your rule is that a run needs at least 75% CSI coverage to count. Right now you only learn the number after the run finishes. On screen you could watch it climb and abort a bad run at second 3 instead of wasting 20 seconds.
3. **Live counters.** Packets sent, CSI rows captured, dropped rows, transmitter failures — the four numbers that decide whether a run is valid.
4. **Pass / fail verdict.** Big green PASS or red FAIL the moment the run ends, with the reason ("coverage 61%, below 75%"). This is your validation rules made visible.
5. **Aiming and setup aid.** A simple live line of signal strength over time, so you can see when the two boards are stable and the room is quiet *before* you start a run. This directly supports your "controlled area" rules about fixed positions. Note: one summary number over time, not raw CSI.

### Useful later — once a trained model exists

6. **The result display.** METAL / NON-METAL / UNCERTAIN with a confidence bar. Your own storage notes are strict that this must show `MODEL NOT TRAINED` until a model genuinely exists, and that stays true.
7. **Standalone operation.** No laptop at the gate. This is the difference between a demo that looks like a science experiment and one that looks like a product — which matters for a competition.
8. **Error and status surface.** Transmitter not answering, card missing, card full, link lost.

---

## 3. The microSD card slot

This is more valuable than the screen, honestly. Seven things it can do:

1. **Store the CSI captures directly**, removing the laptop from the collection loop entirely. This is the "optional microSD logging" already on your roadmap.
2. **Carry the trained model.** Put `model.json` on the card. Retrain on the PC, swap the card, done — no reflashing the board to update the detector. For a competition that is a very clean story.
3. **Hold the object-name list.** Typing an object name on a resistive touchscreen is miserable. Keep a text file of preset names on the card and show them as a scrollable list of buttons instead.
4. **Keep a run log / audit trail** — every run, its settings, its verdict, its reason. This matches the discipline already visible in your change log.
5. **Store the baseline "empty room" reference** so the device can compare a live reading against a known-quiet capture.
6. **Hold interface assets** — a logo, icons, fonts. Purely cosmetic, but it makes the prototype look finished.
7. **Write in exactly the same CSV and JSON format the PC tool already produces.** This is the important one: if the card files match the existing naming and metadata schema, then `process_csi.py`, `extract_features.py`, `dataset_report.py` and the trainer all keep working unchanged. You pull the card, drop the files into `data/`, and the whole existing pipeline just runs. Nothing you have built gets invalidated.

### One serious caution about writing to the card during a capture

Writing to an SD card is slow and it blocks — a single write can stall for tens of milliseconds. Your receiver holds only **two** CSI frames in its internal waiting line before it starts dropping them, and a dropped frame invalidates the whole run by your own rules. So a card write landing at the wrong moment could silently ruin captures.

Three ways round it, roughly in order of effort:

- Hold the run in memory and write the card only *after* the burst ends. Limit: a 20-second run at 20 Hz is about 350 KB as text, which will not fit in the ESP32's available memory.
- Store the run on the card in a compact binary form during capture (about 300 bytes per row instead of 880) and convert it to your CSV format afterwards. Fits comfortably, and the CSV your Python tools expect still comes out the other end.
- Deepen the receiver's internal waiting line from 2 frames to something like 32, so a slow card write gets absorbed instead of causing drops.

The good news: your firmware already counts dropped frames, so whichever route you take is directly measurable rather than guesswork.

---

## 4. Pins

### The one hard rule

**GPIO 1 and GPIO 3 are untouchable.** Those are the USB serial line carrying the CSI data stream at 921600 baud. Everything the receiver produces goes out through them. Do not wire anything to those two pins.

Beyond that, the receiver firmware currently uses **no other pins at all**, so the rest of the board is free.

### Recommended wiring (ESP32 classic → 4.0" TFT)

Screen, touch and card all share one three-wire data path, each with its own "you're up now" select line.

| Display board pin | ESP32 pin | Purpose |
|---|---|---|
| VCC | 5V or 3.3V — **check your board's marking** | power |
| GND | GND | ground |
| SCK | GPIO 18 | shared clock |
| SDI (MOSI) | GPIO 23 | shared data, board → screen |
| SDO (MISO) | GPIO 19 | shared data, screen → board |
| CS | GPIO 33 | screen select |
| DC / RS | GPIO 25 | command-or-picture-data flag |
| RESET | GPIO 26 | screen reset |
| LED | GPIO 32 | backlight (allows dimming) |
| T_CLK | GPIO 18 | shared with above |
| T_DIN | GPIO 23 | shared with above |
| T_DO | GPIO 19 | shared with above |
| T_CS | GPIO 21 | touch select |
| T_IRQ | GPIO 27 | touch "someone pressed me" signal (optional) |
| SD_SCK | GPIO 18 | shared with above |
| SD_MOSI | GPIO 23 | shared with above |
| SD_MISO | GPIO 19 | shared with above |
| SD_CS | GPIO 22 | card select |

Ten ESP32 pins in total. Every one of them is a safe general-purpose pin — none are the "boot-mode" pins that can stop the board starting if something is wired to them wrongly.

The three select lines (GPIO 33, 21, 22) are how one shared set of wires serves three devices: only one device is selected at a time, and the other two ignore the traffic. *As an analogy: it is like VMAS having one encoding session open at a time — the teacher request id says whose turn it is, and everyone else waits.*

### Pins to avoid on this chip, and why

- **GPIO 1, 3** — your CSI data stream. Already covered.
- **GPIO 6–11** — physically wired to the chip's internal storage. Unusable.
- **GPIO 0, 2, 12, 15** — "boot-mode" pins. The chip reads their voltage at power-on to decide how to start. Wire them carelessly and the board refuses to boot. My table avoids all four.
- **GPIO 34–39** — input only. They cannot drive a screen line.

### Power warning

The 4.0" backlight draws roughly 100–150 mA, and the WiFi radio spikes on top of that. A weak USB cable or a laptop port near its limit can cause the board to brown out — which looks like random crashes and corrupted runs, and would be maddening to debug mid-collection. Use a good cable and supply, and run the backlight through GPIO 32 so you can dim it rather than always running it at full.

### Software library

**TFT_eSPI** is the one to use — it supports the ST7796S chip, the resistive touch controller, and is by far the most documented. In your PlatformIO setup the pin numbers go into `build_flags` in `platformio.ini` rather than editing a library file. (LovyanGFX is a faster, cleaner alternative if TFT_eSPI's configuration becomes annoying.)

---

## 5. Proposed on-screen interface

480 × 320, landscape. A consistent frame across every screen:

```
+------------------------------------------------+
|  status bar                          480 x 40  |   link · card · mode
+------------------------------------------------+
|                                                |
|  content area                       480 x 230  |
|                                                |
+------------------------------------------------+
|  action bar                          480 x 50  |   1-3 big buttons
+------------------------------------------------+
```

Buttons no smaller than 60 × 60. Text no smaller than about 16 pixels tall. Resistive touch plus a gymnasium plus nervous hands means generous targets.

### Screen 1 — Boot / self-test

Four checks, ticked off as they pass: receiver ready, linked to CSI_TX, transmitter answering, card present. This maps onto a command your firmware **already has** (`CSI_STATUS`, which replies with ready and link flags), so this screen needs almost no new firmware.

### Screen 2 — Home

Status strip across the top: link, card space remaining, last run's verdict. Content area shows the last result large. Action bar: `COLLECT DATA` and `IDENTIFY`.

Until a model exists, IDENTIFY opens straight onto a plain `MODEL NOT TRAINED` panel. Your own rules require that, and it is also the honest thing to show a competition judge.

### Screen 3 — Label picker (collect mode)

Three large buttons filling the content area: **EMPTY**, **METAL**, **NON-METAL**, with a smaller `OTHER` below. Then object name from a scrollable list read off the SD card — no typing.

### Screen 4 — Run settings

Duration and rate, as preset buttons rather than a number pad: 5s / 10s / 20s, and 20 Hz / 50 Hz. Big confirmation line at the bottom showing exactly where the run will be saved before you commit.

### Screen 5 — Scanning

The screen that matters most. A progress bar, seconds remaining, and four live numbers in fixed boxes: packets sent, rows captured, coverage %, drops. Coverage shown large and colour-coded — green above 75%, red below — so you can see a bad run early.

**Design constraint:** during an active capture, only these small number boxes may repaint. A full-screen redraw takes long enough to starve the CSI waiting line and cause drops. Full repaints happen only between runs.

### Screen 6 — Verdict

Collect mode: a full-screen **PASS** or **FAIL**, the run's filename, and the reason in plain words ("coverage 61%, minimum is 75%"). Buttons: `KEEP` / `DISCARD` / `RUN AGAIN`.

Identify mode (later): the class, a confidence bar, and a `SCAN AGAIN` button.

### Screen 7 — Signal check

A rolling line of signal strength with a "stable / unstable" indicator, for positioning the boards and confirming the room is quiet before a collection session.

### Screen 8 — Error

One clear sentence, one suggested fix, one `RETRY` button. Covers: transmitter silent, link lost, card missing, card full, frames dropped.

---

## 6. The honest risk, stated plainly

Your own roadmap puts the TFT at step 7, *after* the classifier works. There is a real argument that adding the screen now is early — it adds wiring, a library, and timing pressure to a board whose timing behaviour you have spent weeks hardening.

The counter-argument, and the reason I think it is still worth starting: the thing actually blocking you is that no controlled data has been collected yet, and the screen makes that collection faster and less error-prone. So the screen pays for itself *now* if it is built as a collection tool first and a result display second.

The specific technical risk to watch: the receiver's CSI waiting line holds only two frames. Screen drawing and card writing both take time. Do either at the wrong moment and frames drop, which invalidates runs. Mitigations are known (small partial redraws during capture, deeper waiting line, binary-then-convert card writes), and your drop counter makes the whole thing measurable rather than a matter of opinion.

---

## 7. Open questions

- Does the display go on the **receiver board** (your current written plan), or on a **third dedicated board** that talks to the receiver using the text commands it already understands? The second keeps your hardened receiver firmware completely untouched, at the cost of one more board.
- Is your student comfortable taking this on, or is this something you want scoped as a separate piece of work?
- Do you have a second ESP32 spare, in case the third-board route turns out to be the safer one?
