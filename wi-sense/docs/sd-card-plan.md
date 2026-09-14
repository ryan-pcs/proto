# Plan — memory card storage

*Written 2026-09-13. The card slot on the back of the 4.0" display, used to save
captures so the laptop is no longer needed during collection.*

> **2026-09-14 — parked, not gone.** The S3 bench board turns out to have its
> own onboard card reader, which needs none of the wire-sharing this plan
> works around. `src/storage.cpp` was rewritten for that instead — see
> `docs/START-HERE.md`. This plan becomes relevant again only if the classic
> ESP32 ends up as the receiver, since that board has no onboard reader and
> this slot is its only option.

---

## The honest starting point

The display prototype has no real readings in it. Its numbers are invented. So
writing them to a card proves the card works and proves the file format is
right, but saves nothing scientifically useful.

That is still worth doing, and worth doing first, because the card and the file
format are exactly the parts that are fiddly. Getting them settled while the
data is fake means that when the real readings arrive, only one thing is new.

The work splits into three stages, in this order.

---

## Stage A — prove the card works

Goal: write a file, switch the power off and on, read it back.

**Wiring.** The card slot has its own four pads on the back of the display
board: `SD_CS`, `SD_MOSI`, `SD_MISO`, `SD_SCK`. Three of them can share wires
that are already run:

| Card pad | ESP32 pin |
|---|---|
| SD_SCK | GPIO 18 (already used by SCK) |
| SD_MOSI | GPIO 23 (already used by SDI) |
| SD_MISO | GPIO 19 (already used by SDO) |
| SD_CS | GPIO 17 (new) |

So one new wire, plus three more taps onto pins that already carry two wires
each. GPIO 18, 23 and 19 will then carry three wires apiece.

First check whether those pads are pins you can push a jumper onto, or bare
pads that need soldering. On some of these boards they are pads only.

**The known risk.** Sharing one set of wires between the screen, the touch panel
and the card works on many of these boards and is unreliable on some — the
screen does not always release the shared line cleanly when it is not the one
being spoken to. If the card proves flaky, the fallback is to give it its own
set of wires: `SD_SCK` to GPIO 5, `SD_MOSI` to GPIO 16, `SD_MISO` to GPIO 34,
`SD_CS` to GPIO 17. Four wires instead of one, and no sharing at all.

Try the shared version first. It costs one wire to find out.

**Test.** A small program that mounts the card, writes a line to a text file,
reads it back, and reports the card's size and free space on screen. That is
the whole of Stage A. If it passes, the card is dependable.

**Card itself.** Use a small card, 8 or 16 GB, formatted FAT32. Very large cards
and exFAT cause avoidable trouble.

---

## Stage B — write the real file format

Goal: produce files on the card that the existing Python tools accept without
being changed.

This is the part that protects the work already done. `process_csi.py`,
`extract_features.py`, `dataset_report.py` and the trainer all expect a
particular naming and a particular set of metadata fields. If the card writes
files in that shape, the pipeline keeps working: pull the card, drop the files
into `data/`, run the tools as before.

**First step is a reading task, not a writing one.** Go through the Python
tools and write down exactly what they require: the filename pattern, every
field the JSON must contain, and which of those fields the validator actually
checks. Until that list exists, anything written to the card is a guess.

**The timestamp problem.** The filenames currently carry a date and time, and
the ESP32 has no clock — it does not know the date, and forgets everything when
the power goes off. Three ways round it:

- A sequential run number instead of a timestamp, recorded in the metadata. No
  extra hardware. The wall-clock date is added when the files reach the PC.
- A small clock module wired to the same two pins as the distance sensor. A few
  dollars, and then the board genuinely knows the date.
- The PC sets the clock whenever it is connected, and the board holds the time
  until the next power cut.

The first is enough to get going, and does not block anything. Worth confirming
that the Python validator is content with it before committing.

---

## Stage C — save real captures

Only reachable once the display code and the receiver firmware are one program.

**The memory constraint, which decides the design.** A 20-second run at 20 Hz
produces about 400 readings. As text, the format the PC tools expect, that is
roughly 350 KB — more than the classic ESP32 has free. It will not fit in
memory.

Three ways to handle it:

- **Write to the card as the run goes.** Simplest, and the most dangerous: a
  card write can stall for tens of milliseconds, and the receiver only holds two
  readings before it starts dropping them. A dropped reading invalidates the run
  by your own rules.
- **Hold the run in memory in compact form, write afterwards.** Each reading is
  about 256 bytes as raw numbers instead of 880 as text, so a 20-second run at
  20 Hz is around 100 KB — which does fit. Convert to the text format after the
  run ends, when nothing is time-critical. This is the one I would choose.
- **Deepen the receiver's holding queue** from two readings to thirty or so, so
  a slow card write is absorbed rather than causing drops.

The second and third combine well. Note that 20 seconds at 50 Hz is 1000
readings, around 256 KB, which does not fit — so either long runs stay at the
lower rate, or the run is written to the card in chunks between readings.

**Measure before designing.** The 256 bytes above is an estimate. The first task
in this stage is to print the actual size of one reading from the receiver and
work from the real number.

---

## Recommended order

1. Wire the card, shared bus, one new wire to GPIO 17.
2. Write the Stage A test: mount, write, power-cycle, read back.
3. Read the Python tools and write down the exact filename and metadata contract.
4. Write that format to the card using the prototype's invented numbers, and
   check the Python tools accept the files.
5. Measure the true size of one real reading on the receiver.
6. Only then design the buffering, and merge the display code into the receiver.

Steps 1 to 4 can all be done on the board already on your desk, with no
transmitter, no receiver firmware and no radio work.

---

## Transmitter on the ESP32-S3 — decided 2026-09-13

**Yes, and it is the low-risk way round.**

The transmitter does no CSI work at all. It creates the WiFi network and sends
packets; every measurement happens at the receiver. So the transmitter's chip
does not affect the CSI format, the Python tools, or any of the validation work
already done. Its firmware uses no pins beyond power, and is ordinary WiFi code
that should build for the S3 with only a new build profile added — the same
two-profile arrangement already used in the display folder.

Putting the S3 on the *receiver* would be the risky direction: the receiver is
where CSI is captured, its data format differs between chips, and that is the
firmware that has been hardened over weeks.

**One consequence to be aware of.** The measurements depend on the hardware at
both ends. Readings taken with an S3 transmitting are not interchangeable with
readings taken with a WROOM transmitting. So once this choice is made, it is
fixed for the whole dataset — swapping back later would mean recollecting
everything.

Since almost no real data exists yet, now is the free moment to decide. Later it
is not free.

**What is given up.** The S3's 8 MB of extra memory would have solved the Stage C
memory problem outright, had it been the receiver. On the transmitter that
memory goes unused. That is the price of the safer arrangement, and it is
payable — the compact-buffer approach in Stage C works on the classic chip.
