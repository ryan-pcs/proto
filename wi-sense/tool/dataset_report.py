"""Validate labeled CSI captures and report whether training is possible."""

import argparse
import json
from collections import Counter
from pathlib import Path


REQUIRED_METADATA = ("object_name", "samples", "capture_status")
TRAINING_CATEGORIES = ("metal", "non_metal")


def parse_args():
    parser = argparse.ArgumentParser(description="Report CSI dataset readiness")
    parser.add_argument("--data-dir", type=Path, default=Path("data"))
    return parser.parse_args()


def inspect(data_dir):
    report = {"categories": Counter(), "valid_runs": [], "invalid_runs": [], "missing_pairs": []}
    for metadata_path in sorted(data_dir.rglob("*.json")):
        if metadata_path.name.endswith(".features.json"):
            continue
        csv_path = metadata_path.with_suffix(".csv")
        try:
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            missing = [key for key in REQUIRED_METADATA if key not in metadata]
            if "category" not in metadata and "label" not in metadata:
                missing.append("category")
            valid = not missing and csv_path.exists() and metadata.get("samples", 0) > 0 and metadata.get("capture_status") == "success"
            category = metadata.get("category", metadata.get("label", "unknown"))
            if valid:
                report["categories"][category] += 1
                report["valid_runs"].append(str(metadata_path))
            else:
                report["invalid_runs"].append({"file": str(metadata_path), "missing": missing, "csv_exists": csv_path.exists()})
        except (OSError, json.JSONDecodeError) as error:
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
