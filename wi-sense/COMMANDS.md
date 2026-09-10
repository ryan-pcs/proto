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

1. Collect controlled `empty`, `metal`, and `non_metal` recordings.
2. Add exact UDP sequence correlation and loss validation. Current firmware filters CSI to data frames from the connected transmitter BSSID and the collector records CSI coverage, requires at least 75% coverage, and rejects malformed bursts, but this is not exact UDP-packet identity.
3. Validate raw captures, filtering, and feature files as one dataset workflow.
4. Keep training and test recordings separate.
5. Train and evaluate a classifier using only controlled labeled data.
6. Move preprocessing and the tested classifier onto the receiver ESP32.
7. Add the TFT control and result interface.
8. Add optional microSD logging.

The current system must not claim metal detection or confidence until a model
has been trained and tested.
