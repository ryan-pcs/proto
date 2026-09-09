# Wi-Sense Handoff and Commands

This is the current project handoff for a new chat. The project is an ESP32 CSI
prototype with a terminal-first collection workflow. The immediate objective is
to produce non-empty CSI CSV files labelled `empty`, `metal`, and `non_metal`.
Object identification is a later machine-learning phase; no classifier exists.

## Current Truth

The boards currently appeared as:

```text
COM3 = transmitter
COM4 = receiver
```

COM numbers are not permanent. The panel probes ports with `STATUS` and falls
back to manual selection. Close every serial monitor before running Python or
uploading firmware because one process must own a COM port at a time.

Confirmed by the direct radio diagnostic after the UDP-unicast fix:

```text
Transmitter: packets=50, success=50, fail=0
Receiver: CSI_DATA_COUNT=12
```

The working transport is now UDP through the channel-11 `CSI_TX` access point.
The transmitter sends unicast packets to the receiver at `192.168.4.2`.
The receiver joins the AP, enables CSI, and emits `CSI_DATA,` records at
921600 baud. The old ESP-NOW path is retained only in historical notes and is
not used by the current firmware.

The transmitter source was changed from ESP-NOW to UDP through a channel-11
access point (`CSI_TX`). The current images have been flashed and tested. The
receiver reports:

```text
CSI_CONFIG,result=ESP_OK
CSI_CALLBACK,result=ESP_OK
CSI_ENABLE,result=ESP_OK
```

## Objectives

1. Flash the current transmitter and receiver images.
2. Confirm the transmitter is running `mode=udp_ap`.
3. Confirm COM4 emits `CSI_DATA,` during a low-rate burst.
4. Save non-empty labelled CSV files with matching JSON metadata.
5. Only then add filtering, feature extraction, and AI classification.

## Project Map

```text
wi-sense/
	receiver/                 ESP32 receiver firmware
	transmitter/              ESP32 transmitter firmware
	tool/                     Terminal tools and parsers
	tool/data/                Legacy tool-local data folder
	../data/                  Current panel/collector output folder
	COMMANDS.md               This handoff
	STORAGE.md                Storage rules
```

### Receiver firmware

`receiver/main/app_main.c`

- `wifi_init`: starts STA Wi-Fi, disables power save, sets channel 11 and HT20.
- Connects to the transmitter AP `CSI_TX` with password `csi12345`.
- Waits for Wi-Fi association before reporting receiver readiness.
- `wifi_csi_rx_cb`: formats CSI records as `CSI_DATA,...` serial lines.
- `wifi_csi_init`: enables promiscuous mode, configures CSI, registers the CSI callback, and enables CSI.
- `receiver_init`: initializes NVS, Wi-Fi association, and CSI.

`receiver/main/arduino_entry.cpp`

- Sets UART0 to `921600`.
- Calls `receiver_init()` from Arduino `setup()`.

### Transmitter firmware

`transmitter/main/app_main.cpp`

- `print_status`: prints burst state and counters.
- `start_burst`: validates duration/rate and starts timing.
- `stop_burst`: ends the burst and prints counters.
- `handle_command`: handles `START`, `STOP`, and `STATUS`.
- `process_serial_commands`: reads newline-delimited commands.
- `wifi_init`: configures the channel-11 transmitter Wi-Fi mode.
- `setup`: starts serial and Wi-Fi.
- `loop`: processes commands and sends timed packets.

The transmitter uses `WiFi.softAP("CSI_TX", "csi12345", 11, ...)` and sends
UDP packets to `192.168.4.2:4210`. `STATUS` must report `mode=udp_ap` before
trusting the running image.

### Terminal tools

`tool/control_panel.py`

- Boxed interactive menu.
- Auto-detects transmitter by sending `STATUS` to candidate ports.
- Prompts for duration, rate, label, and optional run name.
- Calls `collect_burst.py` for both-board collection.

`tool/control_transmitter.py`

- `parse_args`: parses `ports`, `burst`, `status`, and `stop` commands.
- `send_command`: sends a command and reads protocol responses.
- `main`: validates arguments and opens the transmitter port.

`tool/collect_burst.py`

- `parse_args`: command-line collection arguments.
- `utc_now`: creates UTC metadata timestamps.
- `header_for`: chooses a CSV header based on CSI field count.
- `read_complete_lines`: buffers fragmented serial bytes until newline.
- `open_receiver`: tries receiver baud rates and waits for receiver readiness.
- `collect`: controls the burst, captures `CSI_DATA`, writes CSV and JSON, and reports sample counts.
- `main`: converts collection exceptions into a readable failure.

Important current behavior:

- Transmitter baud: `115200`.
- Receiver baud: auto, tries `921600` then `115200`.
- Maximum duration: `60000` ms / 60 seconds.
- Maximum requested rate: `1000` Hz.
- Recommended first test: 5 seconds at 10 Hz or 50 Hz.
- A CSV with `samples: 0` is an invalid capture.
- The collector waits for both `WiFi connected to CSI_TX` and `CSI ready` when
	those startup messages are available. It can also use an already-running
	receiver at the known 921600 baud.

`tool/collect_csi.py`

Simple receiver-only CSV collector. It is legacy and defaults to `115200`; use
`collect_burst.py` for the current workflow.

`tool/csi_data_read_parse.py`

Legacy graphical parser using PyQt/numpy. It parses CSI arrays and can display
amplitude/phase, but it is not the preferred terminal workflow.

`tool/live_burst_test.py`

Older direct serial test. It uses fixed ports and is useful only for debugging;
prefer `radio_diagnostic.py` or `collect_burst.py`.

`tool/radio_diagnostic.py`

Direct hardware test. It opens COM3 at 115200 and COM4 at 921600, sends a 5
second 10 Hz UDP burst, and counts `CSI_DATA` output.

`tool/csi_viewer.html`

Browser UI. It is deprioritized and should not be used for the current prototype
unless terminal collection is unavailable.

## Flashing

From the workspace root:

```powershell
& "C:\Users\ryan\.platformio\penv\Scripts\pio.exe" run -d ".\wi-sense\receiver" -t upload --upload-port COM4
& "C:\Users\ryan\.platformio\penv\Scripts\pio.exe" run -d ".\wi-sense\transmitter" -t upload --upload-port COM3
```

Do not proceed until each command ends with:

```text
[SUCCESS]
```

If upload stays at `Connecting...`, close monitors and hold the board's `BOOT`
button while starting upload. Release it after connection begins.

Monitor ports directly:

```powershell
& "C:\Users\ryan\.platformio\penv\Scripts\pio.exe" device monitor -p COM3 -b 115200
& "C:\Users\ryan\.platformio\penv\Scripts\pio.exe" device monitor -p COM4 -b 921600
```

Expected receiver startup:

```text
Receiver startup
NVS ready
WiFi ready on channel 11, HT20
WiFi connected to CSI_TX
PROMISCUOUS,result=ESP_OK
CSI_CONFIG,result=ESP_OK
CSI_CALLBACK,result=ESP_OK
CSI_ENABLE,result=ESP_OK
CSI ready; waiting for UDP traffic from CSI_TX
```


## Testing Sequence

1. Close both monitors.
2. Flash receiver COM4 and transmitter COM3.
3. Verify transmitter status:

```powershell
python .\wi-sense\tool\control_transmitter.py --port COM3 status
```

The current UDP source should eventually report `mode=udp_ap` in `STATUS`.

4. For the normal workflow, run the control panel:

```powershell
python .\wi-sense\tool\control_panel.py
```

5. Choose `2`, enter a duration and rate, select a label, and start collection.
	The panel waits for receiver readiness before sending `START`.

For a raw diagnostic, send a low-rate burst:

```powershell
python .\wi-sense\tool\control_transmitter.py --port COM3 burst --seconds 5 --rate 10
```

6. Look for one or more lines beginning with:

```text
CSI_DATA,
```

7. Keep only runs whose JSON says `capture_status` is `success` and whose
	`samples` value is greater than zero.

## Raw device commands

These are sent to COM3 at 115200:

```text
STATUS
START 5000 10
STOP
```

`START` format:

```text
START duration_in_milliseconds packets_per_second
```

Examples:

```text
START 5000 10
START 10000 50
START 60000 100
```

## Storage and Result Meaning

Collection output is saved under the current working directory's `data` folder:

```text
data/<run_name>_<YYYYMMDD_HHMMSS>.csv
data/<run_name>_<YYYYMMDD_HHMMSS>.json
```

Failed or empty runs are automatically deleted by `collect_burst.py` when their
sample count is zero. Older failed runs that were retained for troubleshooting
are stored in:

```text
data/archive/empty_runs/
```

The archive keeps those older CSV and matching JSON files together for
troubleshooting. Do not use archived runs for training.

The CSV contains complete receiver lines beginning with `CSI_DATA,`. It is
lightly parsed into columns but the signal values are not filtered, smoothed,
normalized, or classified.

The JSON records label, timestamps, ports, baud rates, duration, rate,
transmitter packet count, receiver sample count, and completion status.

Valid result:

```text
Receiver CSI samples: greater than 0
Capture status: success
```

Invalid result:

```text
Receiver CSI samples: 0
Capture status: no_csi_received
```

`stored` in older JSON files only meant that metadata was written. New metadata
uses `raw_data_stored` and `capture_status` so an empty CSV is not reported as a
successful dataset.

## Problems Encountered

### Empty CSV files

Earlier runs used ESP-NOW frames. The receiver saw those frames, but the CSI
callback did not fire. The current UDP-unicast path produces CSI successfully.

### Historical packet counts

Older firmware counted send attempts. The current transmitter reports packet,
success, and failure counts separately.

### Serial baud mismatch

Receiver source/configuration used `921600`, while older tools used `115200`.
The collector now probes both, but direct monitoring should use `921600` for the
current receiver image.

### Fragmented CSI records

CSI rows are large. Serial reads can split one record across multiple reads. The
collector now buffers bytes until newline before parsing.

### Receiver reset timing

Opening COM4 can reset an ESP32. The collector waits for receiver association
and CSI initialization before starting a burst, with a fallback for an already
running receiver at 921600 baud.

### Upload and shell confusion

Several tests were accidentally typed into an already-running interactive panel
or serial monitor. Repeated menu text was terminal input echo, not sensor data.
Some terminals also lost `python`/`pio` from PATH. Absolute paths are reliable:

```powershell
C:\Users\ryan\AppData\Local\Programs\Python\Python313\python.exe
C:\Users\ryan\.platformio\penv\Scripts\pio.exe
```

### Stale firmware

An upload command that stops at `Connecting...` has not flashed the board. Do not
trust source changes until PlatformIO prints `[SUCCESS]` and a runtime banner or
`STATUS` field proves the new image is active.

## Do Not Do Yet

- Do not train a model from empty CSV files.
- Do not call a label an automatic classification; labels are human-provided.
- Do not raise the rate above 100 Hz until CSI output is confirmed.
- Do not judge the collector until raw `CSI_DATA,` lines appear in COM4.
- Do not run a serial monitor and Python collector on the same COM port.

## Next Chat Prompt

Paste this into a new chat:

```text
Read wi-sense/COMMANDS.md first. The ESP32 project uses COM3 as transmitter
and COM4 as receiver. The current transport is UDP unicast from the `CSI_TX`
access point to receiver address `192.168.4.2` on channel 11. Verify `STATUS`
reports `mode=udp_ap`, then run the control panel and keep only captures whose
JSON reports `capture_status=success` and `samples > 0`.
```
