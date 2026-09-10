"""Train a small baseline classifier only when real labeled features exist."""

import argparse
import hashlib
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
)


def parse_args():
    parser = argparse.ArgumentParser(description="Train a baseline CSI classifier")
    parser.add_argument("--data-dir", type=Path, default=Path("data"))
    parser.add_argument("--test-data-dir", type=Path, required=True, help="Held-out labeled feature directory")
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


def load_features(metadata_path):
    feature_path = metadata_path.with_name(metadata_path.stem + ".filtered.features.json")
    features = json.loads(feature_path.read_text(encoding="utf-8"))
    return feature_vector(features)


def fit_scaler(vectors):
    means = [sum(row[index] for row in vectors) / len(vectors) for index in range(len(FEATURE_NAMES))]
    deviations = []
    for index, mean in enumerate(means):
        variance = sum((row[index] - mean) ** 2 for row in vectors) / len(vectors)
        deviations.append(math.sqrt(variance) or 1.0)
    return {"mean": means, "std": deviations}


def scale_vector(vector, scaler):
    return [
        (value - mean) / deviation
        for value, mean, deviation in zip(vector, scaler["mean"], scaler["std"])
    ]


def predict(vector, classes, scaler):
    scaled = scale_vector(vector, scaler)
    return min(classes, key=lambda category: distance(scaled, classes[category]))


def capture_signatures(report):
    signatures = {"run_ids": set(), "csv_hashes": set(), "object_names": set()}
    for metadata_path in map(Path, report["valid_runs"]):
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        digest = hashlib.sha256(metadata_path.with_suffix(".csv").read_bytes()).hexdigest()
        signatures["run_ids"].add(metadata.get("run_id", metadata_path.stem))
        signatures["csv_hashes"].add(digest)
        signatures["object_names"].add((metadata["category"], metadata["object_name"]))
    return signatures


def evaluate(model, test_data_dir):
    report = inspect(test_data_dir)
    if not report["training_ready"]:
        raise ValueError("evaluation requires valid metal and non_metal test runs with feature files")

    counts = {category: {label: 0 for label in TRAINING_CATEGORIES} for category in TRAINING_CATEGORIES}
    correct = 0
    total = 0
    for metadata_path in map(Path, report["valid_runs"]):
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        expected = metadata["category"]
        predicted = predict(load_features(metadata_path), model["classes"], model["scaler"])
        counts[expected][predicted] += 1
        correct += predicted == expected
        total += 1
    return {"accuracy": correct / total, "correct": correct, "total": total, "confusion_matrix": counts}


def train(data_dir, output, test_data_dir):
    report = inspect(data_dir)
    if not report["training_ready"]:
        raise ValueError("training requires valid metal and non_metal metadata files")
    if data_dir.resolve() == test_data_dir.resolve():
        raise ValueError("training and test directories must be different")
    test_report = inspect(test_data_dir)
    if not test_report["training_ready"]:
        raise ValueError("evaluation requires valid metal and non_metal test runs with feature files")
    unsupported_categories = sorted(set(report["categories"]) - set(TRAINING_CATEGORIES))
    if unsupported_categories:
        raise ValueError(f"unsupported training categories: {', '.join(unsupported_categories)}")
    train_signatures = capture_signatures(report)
    test_signatures = capture_signatures(test_report)
    for key in train_signatures:
        overlap = train_signatures[key] & test_signatures[key]
        if overlap:
            raise ValueError(f"training and test data overlap in {key}: {sorted(overlap)}")

    samples = {category: [] for category in TRAINING_CATEGORIES}
    for metadata_path in map(Path, report["valid_runs"]):
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        samples[metadata["category"]].append(load_features(metadata_path))

    if any(not samples[category] for category in TRAINING_CATEGORIES):
        raise ValueError("training requires filtered feature files for both classes")

    all_vectors = [vector for rows in samples.values() for vector in rows]
    scaler = fit_scaler(all_vectors)
    scaled_samples = {
        category: [scale_vector(row, scaler) for row in rows]
        for category, rows in samples.items()
    }
    centroids = {
        category: [sum(row[index] for row in rows) / len(rows) for index in range(len(FEATURE_NAMES))]
        for category, rows in scaled_samples.items()
    }
    model = {
        "schema_version": 2,
        "algorithm": "nearest_centroid",
        "features": FEATURE_NAMES,
        "scaler": scaler,
        "classes": centroids,
    }
    model["evaluation"] = evaluate(model, test_data_dir)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(model, indent=2) + "\n", encoding="utf-8")
    return model


def main():
    args = parse_args()
    try:
        model = train(args.data_dir, args.output, args.test_data_dir)
    except (KeyError, OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(f"Training blocked: {error}") from error
    print(f"Model saved: {args.output}")
    print(f"Classes: {', '.join(model['classes'])}")
    print(f"Test accuracy: {model['evaluation']['accuracy']:.3f} ({model['evaluation']['correct']}/{model['evaluation']['total']})")


if __name__ == "__main__":
    main()
