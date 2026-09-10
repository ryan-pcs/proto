# CSI Data Storage

Read [PROJECT_GUIDE.md](PROJECT_GUIDE.md) first for the current workflow.
This file defines the storage schema and keeps the historical change log.

This is the storage format for the prototype. It is ready for future identification, but the current project does not identify objects yet.

## Where files go

The current terminal panel and `collect_burst.py` write to the `data` folder
under the current workspace directory:

```text
proto/data/
```

The browser viewer is legacy. If it is used, browser downloads and browser-local
storage are separate from the terminal collector. Keep downloaded files as the
durable dataset.

Example files in the current output folders:

```text
proto/data/
  empty/
    none_empty_20s_20hz_09-10-2026_213357.csv
    none_empty_20s_20hz_09-10-2026_213357.json
```

Example run:

```text
  proto/data/metal/mug_metal_5s_50hz_09-10-2026_120000.csv
  proto/data/metal/mug_metal_5s_50hz_09-10-2026_120000.json
```

## CSV file

The CSV contains complete receiver `CSI_DATA` rows. The collector lightly
parses serial lines into CSV columns, but it does not filter, smooth, normalize,
or classify CSI values. Do not edit raw data manually. A run with zero rows is
invalid for training.

`process_csi.py` creates derived `.filtered.csv` files using a temporal median
filter and moving average. These files are analysis outputs, not replacements
for raw captures. Variable CSI row lengths are preserved; missing positions are
filled with per-position medians before filtering and trimmed back afterward.

`extract_features.py` creates `.features.json` summaries from filtered files.
Feature files are derived analysis artifacts and are not classifier results.

`dataset_report.py` validates capture metadata and reports training readiness.
`train_classifier.py` is a guarded baseline trainer; it refuses to create a
model until both `metal` and `non_metal` have valid captures and feature files.
No model is created from `empty` or `other` data.

## JSON file

The JSON describes the conditions for the matching CSV file:

```json
{
  "schema_version": 2,
  "run_id": "mug_metal_5s_50hz_09-10-2026_120000",
  "category": "metal",
  "object_name": "mug",
  "description": "metal object",
  "duration_ms": 5000,
  "rate_hz": 50,
  "channel": 11,
  "samples": 250,
  "transmitter_packets": 250,
  "raw_data_stored": true,
  "capture_status": "success"
}
```

Required checks before keeping a run:

- `samples` is greater than zero.
- `category` matches the object or environment actually used.
- The transmitter and receiver positions did not change.
- The environment, distance, rate, and duration are recorded.
- The CSV and JSON names match.
- `raw_data_stored` is `true` and `capture_status` is `success`.

## Current labels

- `empty`: no object between the boards.
- `metal`: metal object between the boards.
- `non_metal`: non-metal object between the boards.

The label is the answer provided by the person collecting the data. It is needed later to train a classifier.

## Identification status

The current system does not have a trained model. The Identify mode is only a
prepared interface and must show:

```text
MODEL NOT TRAINED
```

Do not call a result `metal`, `non_metal`, or `empty` until a model has been trained and tested using stored labeled runs.

The first storage goal is simple: collect clean, labeled, non-empty CSV files with matching metadata. Identification comes after that dataset exists.

## Change Log

Keep this record current when code, firmware, or stored data changes so a future chat can reconstruct the project state.

### 2026-09-10

- Updated receiver firmware to initialize UART at 921600 baud and emit `RECEIVER_READY` only after CSI and Wi-Fi are ready.
- Restored collector validation for transmitter failures, incomplete bursts, emergency STOP responses, and receiver readiness diagnostics.
- Removed old capture files under `data/` before collecting a fresh verified run.
- Verified the repaired path with 50 transmitter packets and 52 CSI samples; status was `success`.
- Added `.vscode/c_cpp_properties.json` to point IntelliSense at the PlatformIO Xtensa compiler, Arduino core, and ESP-IDF headers. This removes false missing-header warnings without changing firmware build behavior.
- Improved automatic capture names to include label, duration, rate, and timestamp, making datasets easier to identify without opening metadata.
- Organized new captures into one of four top-level folders: `data/empty`, `data/metal`, `data/non_metal`, or `data/other`. The panel also supports creating a custom data folder. Failed captures are archived under `data/archive/<selected-folder>/`.
- Added object and category details to filenames: `data/<folder>/<object>_<category>_<seconds>_<hz>_<MM-DD-YYYY_HHMMSS>.csv` with matching `.json` metadata.
- Simplified new metadata from the redundant `classification.item`, `classification.category`, and `classification.environment_label` fields to direct `category`, `object_name`, and `description` fields. `schema_version` is now `2`; older JSON files are not rewritten.
- Wrapped long destination paths in the control panel so the full naming format remains visible.
- Added `Q` cancellation during active collection. Cancellation stops the transmitter and deletes the temporary CSV without writing JSON metadata.
- Removed the duplicate category prompt from the control panel. The selected data-folder name now supplies the JSON `category` and filename category automatically.
- Changed failed-capture archiving to use `data/archive/<selected-folder>/` so failures retain the folder context instead of all being placed in `empty_runs`.
- Documented the standalone metal-detector roadmap in `COMMANDS.md`; classifier labels and confidence values remain future outputs, not current metadata.
- Added the dependency-free offline CSI filtering stage. It was validated on 402 real frames without changing the raw capture; controlled-room testing remains necessary before classifier evaluation.
- Added the research system design, controlled-area rules, required parts, and research question to `COMMANDS.md`. TFT, classifier, and microSD outputs remain future stages.
- Clarified that the 4-inch TFT is the planned physical control panel as well as the result display; the Python terminal panel is its current development substitute.
- Defined mutually exclusive PC and TFT controller modes so the future LCD cannot compete for the transmitter command port. The current TFT command link is not implemented yet.
- Documented the standalone boot architecture: receiver ESP32 hosts the TFT, CSI, preprocessing, and future classifier, then controls the transmitter over a future Wi-Fi command endpoint. Python remains development-only.
- Added dependency-free offline feature extraction and validated it on 402 filtered frames. No classifier is trained yet.
- Added dataset readiness reporting and a guarded baseline trainer. Current report: four valid `other` runs, no valid `metal` or `non_metal` runs, training blocked as intended.
- Added the next implementation plan to `COMMANDS.md` and synchronized the collection workflow documentation. The current priority remains controlled class data, then automatic preprocessing, then separate training and testing.
- Added the standalone data/RAM plan: the receiver processes CSI in small windows, the TFT displays control/results, microSD stores optional data, and the PC is used for development training only.
- Consolidated the active project instructions into `PROJECT_GUIDE.md`; `COMMANDS.md` and `tool/DATA_COLLECTION.md` are now short pointers to prevent conflicting documentation.
- Recorded the signal-loss measurement limitation: transmitter send counters and receiver CSI callback counts are different measurements, so exact end-to-end packet loss needs a future receiver UDP sequence counter.
