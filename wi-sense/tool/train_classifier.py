"""Train a small baseline classifier only when real labeled features exist."""

import argparse
import json
import math
from pathlib import Path

from dataset_report import TRAINING_CATEGORIES, inspect

FEATURE_NAMES = (
    "rssi.mean",
    "rssi.std",
    "mean_amplitude.mean",
    "mean_amplitude.std",
    "std_amplitude.mean",
    "std_amplitude.std",
    "max_amplitude.mean",
    "max_amplitude.std",
)


def parse_args():
    parser = argparse.ArgumentParser(description="Train a baseline CSI classifier")
    parser.add_argument("--data-dir", type=Path, default=Path("data"))
    parser.add_argument("--output", type=Path, default=Path("model.json"))
    return parser.parse_args()


def feature_vector(features):
    values = []
    for name in FEATURE_NAMES:
        section, field = name.split(".")
        values.append(float(features[section][field]))
    return values


def distance(left, right):
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(left, right)))


def train(data_dir, output):
    report = inspect(data_dir)
    if not report["training_ready"]:
        raise ValueError("training requires valid metal and non_metal metadata files")

    samples = {category: [] for category in TRAINING_CATEGORIES}
    for metadata_path in map(Path, report["valid_runs"]):
        feature_path = metadata_path.with_name(metadata_path.stem + ".filtered.features.json")
        if not feature_path.exists():
            continue
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        features = json.loads(feature_path.read_text(encoding="utf-8"))
        samples[metadata["category"]].append(feature_vector(features))

    if any(not samples[category] for category in TRAINING_CATEGORIES):
        raise ValueError("training requires filtered feature files for both classes")

    centroids = {
        category: [sum(row[index] for row in rows) / len(rows) for index in range(len(FEATURE_NAMES))]
        for category, rows in samples.items()
    }
    model = {"schema_version": 1, "algorithm": "nearest_centroid", "features": FEATURE_NAMES, "classes": centroids}
    output.write_text(json.dumps(model, indent=2) + "\n", encoding="utf-8")
    return model


def main():
    args = parse_args()
    try:
        model = train(args.data_dir, args.output)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(f"Training blocked: {error}") from error
    print(f"Model saved: {args.output}")
    print(f"Classes: {', '.join(model['classes'])}")


if __name__ == "__main__":
    main()
