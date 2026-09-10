"""Validate labeled CSI captures and report whether training is possible."""

import argparse
import csv
import json
import math
from collections import Counter
from pathlib import Path


REQUIRED_METADATA = (
    "schema_version",
    "object_name",
    "samples",
    "transmitter_packets",
    "capture_status",
    "raw_data_stored",
    "transmitter_failures",
    "complete",
    "sample_coverage_ratio",
    "sample_coverage_acceptable",
    "receiver_capture_armed",
    "receiver_capture_stopped",
    "receiver_queue_drops",
    "processing_status",
)
TRAINING_CATEGORIES = ("metal", "non_metal")
MIN_CSI_COVERAGE_RATIO = 0.75
FEATURE_SECTIONS = ("rssi", "mean_amplitude", "std_amplitude", "max_amplitude")
FEATURE_FIELDS = ("mean", "std", "min", "max")


def parse_args():
    parser = argparse.ArgumentParser(description="Report CSI dataset readiness")
    parser.add_argument("--data-dir", type=Path, default=Path("data"))
    return parser.parse_args()


def validate_features(metadata_path, csv_path, expected_frames):
    filtered_path = csv_path.with_name(f"{csv_path.stem}.filtered.csv")
    feature_path = filtered_path.with_name(f"{filtered_path.stem}.features.json")
    problems = []
    if not filtered_path.exists():
        problems.append("filtered CSV file is missing")
        return problems
    with filtered_path.open(newline="", encoding="utf-8") as filtered_file:
        filtered_frames = sum(1 for _ in csv.DictReader(filtered_file))
    if filtered_frames != expected_frames:
        problems.append(
            f"filtered CSV rows ({filtered_frames}) do not match samples ({expected_frames})"
        )
    if not feature_path.exists():
        problems.append("filtered feature file is missing")
        return problems
    try:
        features = json.loads(feature_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return [f"feature file is invalid: {error}"]
    if features.get("schema_version") != 1:
        problems.append("feature schema_version must be 1")
    if features.get("frames") != filtered_frames:
        problems.append(
            f"feature frames ({features.get('frames')}) do not match filtered CSV rows ({filtered_frames})"
        )
    source_file = features.get("source_file")
    if not source_file or Path(source_file).name != filtered_path.name:
        problems.append("feature source_file does not match filtered CSV")
    for section in FEATURE_SECTIONS:
        summary = features.get(section)
        if not isinstance(summary, dict):
            problems.append(f"feature section {section} is missing")
            continue
        for field in FEATURE_FIELDS:
            value = summary.get(field)
            if not isinstance(value, (int, float)) or not math.isfinite(value):
                problems.append(f"feature {section}.{field} is not finite")
    return problems


def inspect(data_dir):
    report = {"categories": Counter(), "valid_runs": [], "invalid_runs": [], "missing_pairs": []}
    for metadata_path in sorted(data_dir.rglob("*.json")):
        if metadata_path.name.endswith(".features.json"):
            continue
        csv_path = metadata_path.with_suffix(".csv")
        try:
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            missing = [key for key in REQUIRED_METADATA if key not in metadata]
            if "category" not in metadata:
                missing.append("category")
            problems = list(missing)
            if "schema_version" in metadata and metadata.get("schema_version") != 2:
                problems.append("schema_version must be 2")
            if "samples" in metadata and metadata.get("samples", 0) <= 0:
                problems.append("samples must be greater than zero")
            transmitter_packets = metadata.get("transmitter_packets")
            if "transmitter_packets" in metadata and (
                not isinstance(transmitter_packets, int) or transmitter_packets <= 0
            ):
                problems.append("transmitter_packets must be a positive integer")
            if "capture_status" in metadata and metadata.get("capture_status") != "success":
                problems.append("capture_status must be success")
            if "raw_data_stored" in metadata and metadata.get("raw_data_stored") is not True:
                problems.append("raw_data_stored must be true")
            if "transmitter_failures" in metadata and metadata.get("transmitter_failures") != 0:
                problems.append("transmitter_failures must be zero")
            if "complete" in metadata and metadata.get("complete") is not True:
                problems.append("complete must be true")
            if "sample_coverage_ratio" in metadata:
                ratio = metadata.get("sample_coverage_ratio")
                if not isinstance(ratio, (int, float)) or not math.isfinite(ratio) or ratio <= 0:
                    problems.append("sample_coverage_ratio must be finite and greater than zero")
                elif isinstance(transmitter_packets, int) and transmitter_packets > 0:
                    expected_ratio = metadata.get("samples", 0) / transmitter_packets
                    if not math.isclose(ratio, expected_ratio, rel_tol=0, abs_tol=0.000001):
                        problems.append("sample_coverage_ratio does not match samples/transmitter_packets")
                    if ratio < MIN_CSI_COVERAGE_RATIO:
                        problems.append(f"sample coverage is below {MIN_CSI_COVERAGE_RATIO:.0%}")
            if "sample_coverage_acceptable" in metadata and metadata.get("sample_coverage_acceptable") is not True:
                problems.append("sample_coverage_acceptable must be true")
            if "receiver_capture_armed" in metadata and metadata.get("receiver_capture_armed") is not True:
                problems.append("receiver_capture_armed must be true")
            if "receiver_capture_stopped" in metadata and metadata.get("receiver_capture_stopped") is not True:
                problems.append("receiver_capture_stopped must be true")
            if "receiver_queue_drops" in metadata and metadata.get("receiver_queue_drops") != 0:
                problems.append("receiver_queue_drops must be zero")
            if "processing_status" in metadata and metadata.get("processing_status") != "success":
                problems.append("processing_status must be success")
            csv_rows = None
            if csv_path.exists():
                with csv_path.open(newline="", encoding="utf-8") as csv_file:
                    csv_rows = sum(1 for _ in csv.DictReader(csv_file))
                if csv_rows != metadata.get("samples"):
                    problems.append(f"CSV rows ({csv_rows}) do not match samples ({metadata.get('samples')})")
            else:
                problems.append("CSV file is missing")
            if csv_path.exists() and not problems:
                problems.extend(validate_features(metadata_path, csv_path, csv_rows))
            valid = not problems
            category = metadata.get("category", "unknown")
            if valid:
                report["categories"][category] += 1
                report["valid_runs"].append(str(metadata_path))
            else:
                report["invalid_runs"].append({"file": str(metadata_path), "problems": problems, "csv_exists": csv_path.exists()})
        except (OSError, csv.Error, json.JSONDecodeError) as error:
            report["invalid_runs"].append({"file": str(metadata_path), "error": str(error)})
    report["categories"] = dict(report["categories"])
    report["training_ready"] = all(report["categories"].get(category, 0) > 0 for category in TRAINING_CATEGORIES)
    return report


def main():
    args = parse_args()
    report = inspect(args.data_dir)
    print(json.dumps(report, indent=2))
    if not report["training_ready"]:
        print("TRAINING BLOCKED: collect valid metal and non_metal runs first.")


if __name__ == "__main__":
    main()
