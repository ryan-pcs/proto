# CSI Data Storage

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

Example files in the current flat output folder:

```text
proto/data/
  empty_20260910_120000.csv
  empty_20260910_120000.json
```

Example run:

```text
  proto/data/metal_001_20260910_120000.csv
  proto/data/metal_001_20260910_120000.json
```

## CSV file

The CSV contains complete receiver `CSI_DATA` rows. The collector lightly
parses serial lines into CSV columns, but it does not filter, smooth, normalize,
or classify CSI values. Do not edit raw data manually. A run with zero rows is
invalid for training.

## JSON file

The JSON describes the conditions for the matching CSV file:

```json
{
  "run_id": "metal_001_20260910_120000",
  "label": "metal",
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
- The label matches the object actually used.
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
