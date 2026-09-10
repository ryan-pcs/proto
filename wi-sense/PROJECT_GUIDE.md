# Wi-Sense Project Guide

This is the canonical guide for the project. Read this file first.

## Current Status

Working:

- ESP32 transmitter creates the `CSI_TX` access point and sends UDP sensing packets.
- ESP32 receiver connects to `CSI_TX` and emits raw `CSI_DATA` at 921600 baud.
- Python terminal panel collects labeled raw CSI captures.
- Offline filtering and feature extraction tools are available.
- Dataset reporting and guarded classifier training tools are available.

Not ready yet:

- No trained metal/non-metal classifier exists.
- Controlled-room metal and non-metal data has not been collected.
- TFT control link is not implemented.
- Standalone ESP32 preprocessing/classifier is not implemented.
- microSD logging is not implemented.

## Hardware

Most recent port mapping:

```text
COM6 = transmitter
COM3 = receiver
```

COM numbers can change after reconnecting. The control panel probes ports, but
verify the roles before collection. Close serial monitors before using Python.
Only one program may own a serial port at a time.

The transmitter uses channel 11 and creates:

```text
SSID: CSI_TX
Password: csi12345
Receiver address: 192.168.4.2
```

## Start The Terminal Panel

From the workspace root:

```powershell
Set-Location "C:\Users\ryan\Documents\proto"
python .\wi-sense\tool\control_panel.py
```

If `python` is unavailable:

```powershell
& "C:\Users\ryan\AppData\Local\Programs\Python\Python313\python.exe" ".\wi-sense\tool\control_panel.py"
```

In the panel, choose collection, then select:

1. Data folder: `empty`, `metal`, `non_metal`, `other`, or a custom folder.
2. Object name: use `none` for an empty environment.
3. Duration and packet rate.

Every started collection runs to its configured duration and then stops through
the normal cleanup path. Failed bursts are archived rather than treated as
training data.

## Capture Layout

New captures are saved as:

```text
data\<folder>\<object>_<category>_<seconds>_<hz>_<MM-DD-YYYY_HHMMSS>.csv
data\<folder>\<object>_<category>_<seconds>_<hz>_<MM-DD-YYYY_HHMMSS>.json
```

Example:

```text
data\metal\mug_metal_5s_50hz_09-10-2026_120000.csv
data\metal\mug_metal_5s_50hz_09-10-2026_120000.json
```

Filtering and feature extraction run automatically after successful captures.

## Data Workflow

Use this order:

1. Collect multiple controlled `empty`, `metal`, and `non_metal` runs.
2. Use multiple objects per class.
3. Keep TX/RX positions, distance, object placement, room, and frequency consistent.
4. Keep training and test recordings separate.
5. Filter successful raw captures.
6. Extract features.
7. Run the dataset report.
8. Filter a raw capture manually when reprocessing an older run.
9. Evaluate only on recordings not used for training.

Controlled-area rules:

- Fix transmitter and receiver positions.
- Keep distance and object placement fixed.
- Keep people away from the sensing path.
- Record room, distance, activity, object name, category, duration, and rate.
- Do not assume every CSI change is caused by the object.

## Offline Tools

Filter a raw capture without changing it:

```powershell
python .\wi-sense\tool\process_csi.py .\data\empty\<capture>.csv
```

This creates `<capture>.filtered.csv` using a temporal median filter and moving
average.

Extract features:

```powershell
python .\wi-sense\tool\extract_features.py .\data\empty\<capture>.filtered.csv
```

This creates `<capture>.filtered.features.json`.

Check dataset readiness:

```powershell
python .\wi-sense\tool\dataset_report.py --data-dir .\data
```

Train the guarded baseline model:

```powershell
python .\wi-sense\tool\train_classifier.py --data-dir .\data\train --test-data-dir .\data\test --output .\model.json
```

Training is blocked until valid `metal` and `non_metal` captures and feature
files exist in both separate directories. Evaluation accuracy and a confusion
matrix are stored in the model metadata. Do not use `empty` or `other` as
classifier classes.

## Current Metadata

New metadata uses schema version 2:

```json
{
  "schema_version": 2,
  "category": "metal",
  "object_name": "mug",
  "description": "metal object",
  "samples": 250,
  "transmitter_packets": 250,
  "transmitter_failures": 0,
  "capture_status": "success"
}
```

A valid training capture requires a matching CSV, samples greater than zero,
`capture_status` equal to `success`, zero transmitter failures, matching
transmitter/receiver counts, successful processing, and a matching feature
file.

## PC And Future TFT

Development mode:

```text
PC terminal panel -> USB serial -> transmitter
PC collector <- USB serial <- receiver
```

Final standalone mode:

```text
TFT -> receiver ESP32
receiver ESP32 -> Wi-Fi -> transmitter ESP32
receiver ESP32 -> optional microSD
```

The receiver will eventually host the TFT UI, CSI processing, and classifier.
The TFT displays boot, ready, scanning, progress, result, confidence, and
errors. It does not receive or display all raw CSI.

Only one controller may own the transmitter command interface at a time. The
future TFT will replace the PC panel; they must not run simultaneously.

## Standalone Roadmap

```text
Theory
-> working TX/RX
-> raw CSI
-> controlled dataset
-> preprocessing
-> features
-> classifier
-> testing
-> classifier on receiver ESP32
-> TFT interface
-> optional microSD logging
```

Research question:

> Can CSI measurements from this ESP32 transmitter-receiver system distinguish
> metal from non-metal objects in a controlled sensing area?

No current output may claim metal detection or confidence until a trained and
tested model exists.

## Related Files

- `COMMANDS.md`: runnable commands, collection steps, and implementation plan.
- `STORAGE.md`: storage schema, metadata, and derived-file rules.
- `CHANGELOG.md`: permanent record of project changes.
- `receiver/README.md`: receiver build/upload note.
- `transmitter/README.md`: transmitter build/upload note.
- `tool/DATA_COLLECTION.md`: short collection reference.
