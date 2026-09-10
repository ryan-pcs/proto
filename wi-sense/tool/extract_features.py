"""Extract simple CSI features from a filtered CSI CSV."""

import argparse
import csv
import json
import math
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(description="Extract features from filtered CSI data")
    parser.add_argument("input_csv", type=Path, help="Filtered CSI CSV")
    parser.add_argument("--output", type=Path, help="Feature JSON output path")
    return parser.parse_args()


def numbers(rows, field):
    return [float(row[field]) for row in rows if row.get(field) not in (None, "")]


def summary(values):
    if not values:
        return {"count": 0}
    mean = sum(values) / len(values)
    variance = sum((value - mean) ** 2 for value in values) / len(values)
    return {
        "count": len(values),
        "mean": round(mean, 6),
        "std": round(math.sqrt(variance), 6),
        "min": round(min(values), 6),
        "max": round(max(values), 6),
    }


def extract(input_csv, output_json):
    with input_csv.open(newline="", encoding="utf-8") as csv_file:
        rows = list(csv.DictReader(csv_file))
    if not rows:
        raise ValueError("filtered CSV contains no rows")

    features = {
        "schema_version": 1,
        "source_file": str(input_csv),
        "frames": len(rows),
        "macs": sorted({row.get("mac", "") for row in rows if row.get("mac")}),
        "rssi": summary(numbers(rows, "rssi")),
        "mean_amplitude": summary(numbers(rows, "mean_amplitude")),
        "std_amplitude": summary(numbers(rows, "std_amplitude")),
        "max_amplitude": summary(numbers(rows, "max_amplitude")),
    }
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(features, indent=2) + "\n", encoding="utf-8")
    return features


def main():
    args = parse_args()
    output = args.output or args.input_csv.with_name(
        f"{args.input_csv.stem}.features.json"
    )
    try:
        features = extract(args.input_csv, output)
    except (OSError, ValueError, csv.Error) as error:
        raise SystemExit(f"Feature extraction failed: {error}") from error
    print(f"Extracted features from {features['frames']} frames")
    print(f"Feature output: {output}")


if __name__ == "__main__":
    main()
