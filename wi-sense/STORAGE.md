# CSI Data Storage

This is the storage format for the prototype. It is ready for future identification, but the current project does not identify objects yet.

## Where files go

The HTML viewer uses two places:

1. **Downloads folder**: exported `.csv` and `.json` files.
2. **Browser-local storage**: a local IndexedDB copy shown in the Dataset storage panel.

The browser-local copy is not a GitHub backup. Keep the downloaded files as the durable dataset.

Recommended local folder:

```text
wi-sense/tool/data/
```

Organize files by label:

```text
wi-sense/tool/data/
  empty/
  metal/
  non_metal/
```

Example run:

```text
wi-sense/tool/data/metal/metal_001.csv
wi-sense/tool/data/metal/metal_001.json
```

## CSV file

The CSV contains the raw receiver `CSI_DATA` rows. Do not edit the raw data manually. A run with zero rows is invalid for training.

## JSON file

The JSON describes the conditions for the matching CSV file:

```json
{
  "collection_name": "metal_001",
  "label": "metal",
  "mode": "collect",
  "environment": "control_room_01",
  "distance_cm": 100,
  "duration_ms": 5000,
  "rate_hz": 50,
  "channel": 11,
  "samples": 250,
  "notes": "metal plate centered between boards"
}
```

Required checks before keeping a run:

- `samples` is greater than zero.
- The label matches the object actually used.
- The transmitter and receiver positions did not change.
- The environment, distance, rate, and duration are recorded.
- The CSV and JSON names match.

## Current labels

- `empty`: no object between the boards.
- `metal`: metal object between the boards.
- `non_metal`: non-metal object between the boards.

The label is the answer provided by the person collecting the data. It is needed later to train a classifier.

## Identification status

The current system does not have a trained model. The Identify mode is only a prepared interface and must show:

```text
MODEL NOT TRAINED
```

Do not call a result `metal`, `non_metal`, or `empty` until a model has been trained and tested using stored labeled runs.

The first storage goal is simple: collect clean, labeled, non-empty CSV files with matching metadata. Identification comes after that dataset exists.
