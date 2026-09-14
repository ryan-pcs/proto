"""Train a small baseline classifier only when real labeled features exist."""

import argparse
import hashlib
import json
import math
from collections import Counter
from pathlib import Path

from dataset_report import TRAINING_CATEGORIES, inspect

# Must stay in step with GATE_CONF_UNSURE and GATE_CONF_METAL in
# display/src/theme.h. Both are placeholders; see the note where the model
# records them.
DEFAULT_THRESHOLDS = {"unsure": 40, "metal": 75}

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


def metal_score(vector, classes, scaler):
    """How much this capture looks like metal, 0-100.

    A label on its own is not enough for the machine at the gate. It shows
    three levels - clear, unsure, search - and the middle one only exists
    because a capture can sit between the two classes. Something has to say
    how far between, and this is it.

    The number is where the capture falls on the line joining the two class
    centres: 0 sits on the non-metal centre, 100 on the metal centre, and 50
    is exactly between them, which is the machine knowing nothing.

    It is a distance ratio, not a probability. It must not be described as one
    - nearest-centroid has no notion of likelihood, and calling this 87%
    certain would be a claim the method cannot support.
    """
    scaled = scale_vector(vector, scaler)
    to_metal = distance(scaled, classes["metal"])
    to_other = distance(scaled, classes["non_metal"])
    total = to_metal + to_other
    if total == 0:
        return 50.0        # both centres in the same place: no information
    return 100.0 * to_other / total


def band(score, thresholds):
    """The three levels the gate screen shows, from a score."""
    if score >= thresholds["metal"]:
        return "search"
    if score >= thresholds["unsure"]:
        return "check"
    return "clear"


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
    scores = []
    bands = {category: Counter() for category in TRAINING_CATEGORIES}

    for metadata_path in map(Path, report["valid_runs"]):
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        expected = metadata["category"]
        vector = load_features(metadata_path)
        predicted = predict(vector, model["classes"], model["scaler"])
        score = metal_score(vector, model["classes"], model["scaler"])

        counts[expected][predicted] += 1
        bands[expected][band(score, model["thresholds"])] += 1
        scores.append({
            "run": metadata.get("run_id", metadata_path.stem),
            "actual": expected,
            "predicted": predicted,
            "metal_score": round(score, 2),
            "band": band(score, model["thresholds"]),
        })
        correct += predicted == expected
        total += 1

    return {
        "accuracy": correct / total,
        "correct": correct,
        "total": total,
        "confusion_matrix": counts,
        # What the gate screen would actually have shown for each test run.
        # This is the evidence for choosing the two thresholds: if real metal
        # runs cluster well above real non-metal ones, the bands can be moved
        # apart; if they overlap, the orange band has to be wide and honest.
        "bands": {category: dict(counter) for category, counter in bands.items()},
        "scores": sorted(scores, key=lambda row: row["metal_score"]),
    }


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
        "schema_version": 3,
        "algorithm": "nearest_centroid",
        "features": FEATURE_NAMES,
        "scaler": scaler,
        "classes": centroids,
        # Where the gate screen turns a score into green, orange or red. These
        # match the placeholders in display/src/theme.h and are written into
        # the model so the two can never silently drift apart.
        #
        # They are guesses until a model has been tested against bags whose
        # contents are known. Where they belong is the trade between waving
        # through a bag with metal in it and hand-searching bags that had none:
        # a decision about how the gate is run, not a number to pick at a desk.
        # The evaluation below is what the decision should be made from.
        "thresholds": dict(DEFAULT_THRESHOLDS),
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
    evaluation = model["evaluation"]
    print(f"Test accuracy: {evaluation['accuracy']:.3f} ({evaluation['correct']}/{evaluation['total']})")
    print()
    print("Metal score per test run - this is what the gate screen would show:")
    for row in evaluation["scores"]:
        print(f"  {row['metal_score']:6.2f}  {row['band']:<6}  actual {row['actual']:<9}  {row['run']}")
    print()
    print(f"Thresholds used: clear below {model['thresholds']['unsure']}, "
          f"search at or above {model['thresholds']['metal']}")
    overlap = evaluation["bands"].get("non_metal", {}).get("search", 0)
    missed = evaluation["bands"].get("metal", {}).get("clear", 0)
    if overlap or missed:
        print(f"WARNING: {overlap} non-metal run(s) would have been flagged for search, "
              f"and {missed} metal run(s) would have been waved through.")
        print("Move the thresholds, or the two classes are not separable on these features.")

    # A quieter failure, and an easy one to miss: the classifier can be right
    # every time while the gate screen never turns red, because the scores all
    # sit near the middle. A distance ratio between two centres does not spread
    # to the extremes the way a probability would, so thresholds that sound
    # reasonable (40 and 75) can be far too wide for it.
    flagged = evaluation["bands"].get("metal", {}).get("search", 0)
    if evaluation["accuracy"] >= 0.9 and flagged == 0:
        print("NOTE: every run was classified correctly, but no metal run reached the")
        print("search threshold, so the gate would never show red. The thresholds are")
        print("set wider than this scoring method spreads. Use the scores above to")
        print("choose them, and change theme.h to match.")


if __name__ == "__main__":
    main()
