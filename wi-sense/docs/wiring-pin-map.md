# Wiring & Pin Map — Two-Board Build

*Written 2026-09-11. Covers the agreed two-board design: transmitter, and receiver carrying the screen, indicator lights and presence sensor. Read `tft-display-integration-brief.md` first for why the design is shaped this way.*

---

## 1. What goes on which board

**Transmitter ESP32** — needs **no signal wiring at all**. Power and ground only. Its whole job is to create the `CSI_TX` WiFi network and send sensing packets. Nothing is attached to it.

**Receiver ESP32** — carries everything: the 4.0" screen, its touch panel, the three indicator lights, and the presence sensor. It also does the processing and, later, the detection.

---

## 2. The one rule that cannot be broken

**GPIO 1 and GPIO 3 on the receiver must stay empty.** They carry the CSI readings out to the laptop at high speed during data collection. Anything wired there breaks collection. Every assignment below avoids them.

---

## 3. Receiver pin map

### Screen and touch panel

Screen and touch share one set of data wires. Each has its own select line, so only one is listened to at a time.

| Display board pin | ESP32 GPIO | Notes |
|---|---|---|
| VCC | 5V rail | **verify your board's marking first** — see section 6 |
| GND | GND | common ground |
| SCK | 18 | shared clock |
| SDI (MOSI) | 23 | shared data in |
| SDO (MISO) | 19 | shared data out |
| CS | 33 | screen select |
| DC / RS | 25 | command-or-picture flag |
| RESET | 26 | screen reset |
| LED | 32 | backlight, allows dimming |
| T_CLK | 18 | same wire as SCK |
| T_DIN | 23 | same wire as SDI |
| T_DO | 19 | same wire as SDO |
| T_CS | 4 | touch select |
| T_IRQ | 35 | touch press signal — input-only pin, which is all this needs |

### Indicator lights

| Light | ESP32 GPIO | Meaning |
|---|---|---|
| Green | 27 | bag clear |
| Red | 14 | possible metal — guard should look |
| Blue | 13 | ready to scan |

Your Keyestudio modules have three pins each: signal to the GPIO above, `+` to 3.3V, `−` to ground. They have the current-limiting resistor built in, so no extra parts needed.

### Presence sensor (time-of-flight)

| Sensor pin | ESP32 GPIO |
|---|---|
| SDA | 21 |
| SCL | 22 |
| VCC | 3.3V |
| GND | GND |

These are the ESP32's conventional pins for this two-wire sensor connection, so most libraries will find it without extra configuration.

### Still free afterwards

GPIO 5, 16, 17, and input-only 34, 36, 39. Enough headroom for the microSD card slot later (its select line would go on 17, sharing pins 18/23/19 with everything else) plus spares.

---

### Bench board note — ESP32-S3 N16R8

The map above is for the **classic ESP32-WROOM-32**, which is what the two
project boards are and what the receiver will be. It is the authority for the
real build.

The spare board used to develop the screens is an **ESP32-S3 N16R8**, a
different chip that numbers its pins differently. Several numbers above — 22,
23 and 25 — do not exist on the S3 at all, and 26, 27, 32 and 33 are reserved
there for the chip's own memory. So the bench board uses its own table:

| Signal | S3 GPIO |
|---|---|
| SCK, T_CLK | 12 |
| SDI (MOSI), T_DIN | 11 |
| SDO (MISO), T_DO | 13 |
| CS | 10 |
| DC / RS | 14 |
| RESET | 9 |
| LED (backlight) | 8 |
| T_CS | 15 |
| Green light | 4 |
| Red light | 5 |
| Blue light | 6 |
| Sensor SDA | 17 |
| Sensor SCL | 18 |

The display program carries both tables and picks one at build time, so the
same code runs on either board without editing. See `../display/README.md`.

## 4. Transmitter pin map

| Pin | Connection |
|---|---|
| 5V / VIN | 5V rail |
| GND | common ground |

That is the whole list.

---

## 5. Recommended: use the time-of-flight sensor alone

You mentioned possibly an ultrasonic or infrared sensor, *and* a time-of-flight sensor. These do the same job — detect that a bag is present so the machine is not scanning continuously. Use the time-of-flight one and drop the other two.

Why:

- **Infrared obstacle sensors are fooled by dark surfaces.** They work by bouncing light off an object, and a black bag absorbs most of it. A sensor that misses black bags is a bad fit for a bag scanner.
- **Ultrasonic sensors send out a wide cone of sound** that bounces off the tray walls as readily as the bag, causing false triggers. They also output 5 volts on their echo pin, and ESP32 pins tolerate only 3.3 volts — so that pin needs a voltage divider or it will damage the board over time.
- **Time-of-flight gives you an actual distance**, in a narrow beam, at 3.3 volts natively. You can set a real rule — "start a scan only when something sits between 5 and 30 cm above the tray" — instead of just "something is there."

Fewer parts, two pins, better behaviour.

---

## 6. Power

Use the **5V supply**, not the 12V one. Everything in this build runs at 5V or 3.3V, so the 12V would only add a conversion stage, more heat and more parts for no gain. Keep the 12V supply for anything later that genuinely needs it.

Check the 5V supply's current rating before committing. You need **at least 2 amps**. Rough budget:

| Load | Typical | Peak |
|---|---|---|
| Receiver ESP32 | 120 mA | 500 mA |
| Transmitter ESP32 | 120 mA | 500 mA |
| Screen backlight | 120 mA | 150 mA |
| Lights and sensor | 40 mA | 60 mA |
| **Total** | **~400 mA** | **~1.2 A** |

Wiring:

- 5V supply → toggle switch → 5V rail → both ESP32 `VIN`/`5V` pins.
- **One common ground for everything.** Every board, the screen, the lights and the sensor share it. This is the single most commonly missed connection, and the symptoms are confusing rather than obvious.
- A **470 µF** electrolytic capacitor across 5V and ground, positioned close to each ESP32's power pin. These act as a local reserve for the sudden gulp of current the radio takes when it transmits. *As an analogy: the small capacitors your student already has are like a few spare answer sheets kept at the desk; the 470 µF is the box under the table that covers a sudden rush.*
- A **100 nF** ceramic capacitor near each board's 3.3V pin, which is what the small ones already in the drawing are for.

### Verify before powering the screen

Check the marking beside the screen's VCC pin on your actual board. Some of these red modules accept 5V and regulate internally; some accept only 3.3V. Powering a 3.3V-only board from 5V will damage it. If yours is 3.3V-only, take VCC from the receiver's 3.3V pin — but watch for the board getting warm, because a 4-inch backlight is a heavy load for the ESP32's small onboard regulator.

---

## 7. One caution on the red and green lights

Your own project rules are clear that nothing may claim metal detection until a model has been trained and tested. No model exists yet.

So while the wiring can be done now, **the red and green lights must not be driven by a guess** in the meantime. Until there is a tested model, blue works as the ready light and the screen shows `MODEL NOT TRAINED`. A prototype whose red light fires on a hunch would be the one thing that undermines the whole project in front of judges — the value of your work so far is precisely that it refuses to claim what it has not proven.

A smaller note: red and green together is the most common form of colour blindness, affecting roughly one man in twelve. Since a guard reads this at a glance, keep the screen's text verdict as the primary channel and the lights as reinforcement, rather than the other way round.

---

## 8. Before wiring — checklist

1. Confirm the screen's VCC voltage marking on the physical board.
2. Confirm the 5V supply is rated 2 A or more.
3. Confirm nothing is connected to receiver GPIO 1 or GPIO 3.
4. Confirm every part shares one common ground.
5. Add the two 470 µF capacitors — do not skip these because it "works without them" on the bench.

---

## 9. Sensor placement — decided 2026-09-11

**The sensor looks across the tray, and is mounted on the receiver side.**

Looking across is fine. What matters is which end it is mounted at, and the answer is: the same end as the receiver, never the far end.

The reason is the wires, not the sensor. The sensing path is the straight line between the two boards' antennas — the line the WiFi signal travels through the bag. A sensor mounted at the far end would need its four wires run all the way back to the receiver, crossing or skirting that path. Wires are metal. Metal near the path reflects the signal, and loose wire that shifts between runs changes the readings between runs. That would quietly undermine the run-to-run consistency your controlled-area rules exist to protect. Mounted at the receiver end, the wires are a few centimetres long and nothing new sits in the path.

Two placement details:

- **Keep the sensor clear of the ESP32's antenna end.** The antenna sits at one end of the board. Anything conductive within a few centimetres of it changes how it behaves. A few centimetres of separation, and not directly in front of it, is enough.
- **Set the beam lower than the radio path** — near the tray floor rather than at bag-centre height. It still catches a bag entering, and it keeps the two jobs physically separate.

The sensor's "nothing there" reading will be a constant distance, the width of the tray. A bag breaks that. Simple and dependable.

**Cable length note:** the two-wire sensor connection degrades over long runs. Keep it under roughly 30 cm, which the receiver-side mounting gives you naturally.

## 10. Sequencing — finish the build before collecting

Worth stating plainly, because it affects what your student does next.

Every physical change to the rig — adding the sensor, moving a board, changing the tray, adding the screen — changes the radio environment. Recordings made before a change do not match recordings made after it. Mixing them produces a dataset that looks fine and quietly is not.

You have almost no real data yet: only the `other` test captures. That is lucky timing rather than a setback. It means you can build the complete physical rig — boards fixed in place, sensor mounted, screen attached, everything in its final position — and only then start collecting the `empty`, `metal` and `non_metal` runs. Collect first and the work gets thrown away the moment the sensor is screwed on.

So: build, fix everything in position, photograph the arrangement for the record, collect the `empty` baseline last, then the object runs.

## 11. Open questions

- Does the tray design physically fix the distance between transmitter and receiver? The controlled-area rules require it, and the build should enforce it rather than rely on someone remembering.
