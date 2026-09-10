# CSI Data Storage

Read [PROJECT_GUIDE.md](PROJECT_GUIDE.md) first for the current workflow.
This file defines the storage schema, capture layout, metadata, and derived
analysis files. The historical record is maintained in
[CHANGELOG.md](CHANGELOG.md).

This is the storage format for the prototype. It is ready for future identification, but the current project does not identify objects yet.

## Where files go

The current terminal panel and `collect_burst.py` write to the `data` folder
under the current workspace directory:

```text
proto/data/
```

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
- `sample_coverage_acceptable` is `true` and `sample_coverage_ratio` matches
  `samples / transmitter_packets`. New captures require at least 75% CSI
  coverage.
- `receiver_capture_armed` is `true`.
- `receiver_capture_stopped` is `true`.
- `receiver_queue_drops` is zero; any bounded-queue drop invalidates the run.
- `processing_status` is `success` and the matching feature file exists.

Current capture limitation: the receiver filters CSI to data frames from the
transmitter access point, but the current CSI callback does not expose the UDP
payload sequence number. The coverage ratio measures usable CSI callbacks,
not one-to-one UDP packet delivery, so a successful capture does not prove that
every CSI row came from one sensing UDP packet.

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

