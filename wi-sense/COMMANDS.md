# Wi-Sense Commands

Use this file for runnable commands, what each command does, the collection
steps, and the current implementation plan.

Read [PROJECT_GUIDE.md](PROJECT_GUIDE.md) first for hardware roles, project
status, and operating constraints. The permanent project record is in
[CHANGELOG.md](CHANGELOG.md). Storage rules are in [STORAGE.md](STORAGE.md).

## Start The Control Panel

From the workspace root:

```powershell
Set-Location "C:\Users\ryan\Documents\proto"
python .\wi-sense\tool\control_panel.py
```

If `python` is unavailable:

```powershell
& "C:\Users\ryan\AppData\Local\Programs\Python\Python313\python.exe" ".\wi-sense\tool\control_panel.py"
```

Collection steps:

1. Close serial monitors and other programs using either board.
2. Start the control panel.
3. Confirm the detected transmitter and receiver ports.
4. Choose `2` for a labeled CSI burst.
5. Enter duration and packet rate.
6. Choose `empty`, `metal`, `non_metal`, `other`, or a custom folder.
7. Enter the object name; use `none` for an empty environment.
8. Review the destination and press Enter to start.
9. Let the timed burst finish; every started burst is stopped and finalized through the normal cleanup path.

## Process Captures

Filter a raw capture without changing it:

```powershell
python .\wi-sense\tool\process_csi.py .\data\empty\<capture>.csv
```

Extract features from a filtered capture:

```powershell
python .\wi-sense\tool\extract_features.py .\data\empty\<capture>.filtered.csv
```

Check whether the dataset is ready for training:

```powershell
python .\wi-sense\tool\dataset_report.py --data-dir .\data
```

Train the guarded baseline model:

For a held-out evaluation, keep training and test captures in separate
directories and provide both paths:

```powershell
python .\wi-sense\tool\train_classifier.py --data-dir .\data\train --test-data-dir .\data\test --output .\model.json
```

Training requires valid `metal` and `non_metal` captures with matching feature
files in both directories. The command writes accuracy and a confusion matrix
into the model metadata. Do not use `empty` or `other` as classifier classes.

## Build And Upload

Build or upload the receiver:

```powershell
Set-Location .\wi-sense\receiver
pio run
pio run --target upload
```

Build or upload the transmitter:

```powershell
Set-Location ..\transmitter
pio run
pio run --target upload
```

The receiver monitor uses 921600 baud. The transmitter command port uses
115200 baud.

## Implementation Plan

Complete the project in this order:

1. Keep the terminal control panel and existing collection tools working.
2. Collect controlled `empty`, `metal`, and `non_metal` recordings.
3. Keep training and test recordings separate.
4. Validate the raw CSI to filtered data to feature workflow.
5. Add exact UDP sequence correlation and loss validation. Current firmware
   filters CSI to data frames from the connected transmitter BSSID and the
   collector records CSI coverage, requires at least 75% coverage, and rejects
   malformed bursts, but this is not exact UDP-packet identity.
6. Add the time-of-flight sensor to the receiver ESP32 and test a stable
   bag-present distance rule.
7. Implement automatic receiver sequencing: confirm bag presence, arm CSI,
   command the transmitter over WiFi, collect the burst, stop and validate the
   capture, then report the result.
8. Train and evaluate a classifier using only controlled labeled data.
9. Move the tested preprocessing and classifier onto the receiver ESP32.
10. Integrate the teacher's display with receiver status, scan progress, and
    results. The display and `docs/` folders are protected from terminal-tool
    cleanup.
11. Add optional microSD logging after capture timing is stable.

### Automatic Scan Sequence

```text
ToF sensor detects bag
	|
	v
Receiver confirms stable presence
	|
	v
Receiver arms CSI capture
	|
	v
Receiver commands transmitter over WiFi
	|
	v
Transmitter sends UDP sensing burst
	|
	v
Receiver captures and validates CSI
	|
	v
Receiver processes the run and reports the result
```

The presence sensor does not command the transmitter directly. The receiver is
the coordinator because it owns the sensor, CSI capture, validation, and future
classification. The terminal panel remains the development controller until
the standalone receiver path is tested. The terminal panel and future display
must not control the transmitter simultaneously.

### Board Responsibilities

- Transmitter: create `CSI_TX`, send UDP sensing packets, accept start/stop/status
  commands, and report packet results. It does not read the presence sensor or
  make detection decisions.
- Receiver: connect to `CSI_TX`, read the ToF sensor, coordinate scans, capture
  and validate CSI, run future classification, and provide status/results to the
  display.
- Display: provide the standalone user interface. The teacher handles its
  firmware separately.

### Validation Gates

A run is usable only when CSI coverage is at least 75%, transmitter failures
and receiver queue drops are zero, CSI capture was armed and stopped cleanly,
the burst completed, metadata matches the raw CSV, processing succeeded, and
training/test recordings do not overlap.

Until a classifier has been trained and evaluated on separate test data, the
system must not claim `METAL` or `NON-METAL` detection or confidence. Red and
green verdict lights remain disabled; blue is reserved for ready/status.
