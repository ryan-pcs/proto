"""Create filtered CSI data and amplitude summaries without changing raw captures."""

import argparse
import csv
import json
import math
from pathlib import Path


DEFAULT_WINDOW = 3


def parse_args():
    parser = argparse.ArgumentParser(description="Filter a stored CSI capture")
    parser.add_argument("input_csv", type=Path, help="Raw CSI CSV file")
    parser.add_argument("--output", type=Path, help="Derived output CSV path")
    parser.add_argument("--window", type=int, default=DEFAULT_WINDOW, help="Odd temporal filter window")
    return parser.parse_args()


def odd_window(value):
    if value < 1 or value % 2 == 0:
        raise ValueError("window must be a positive odd number")
    return value


def median(values):
    ordered = sorted(values)
    return ordered[(len(ordered) - 1) // 2]


def parse_rows(input_csv):
    rows = []
    with input_csv.open(newline="", encoding="utf-8") as csv_file:
        for row in csv.DictReader(csv_file):
            if not row.get("data"):
                continue
            try:
                values = json.loads(row["data"])
                numeric_values = [float(value) for value in values]
            except (TypeError, ValueError, json.JSONDecodeError):
                continue
            if len(numeric_values) < 2:
                continue
            rows.append((row, numeric_values))
    if not rows:
        raise ValueError("input contains no valid CSI rows")
    return rows


def pad_rows(rows):
    width = max(len(values) for _, values in rows)
    column_medians = [
        median([values[column] for _, values in rows if column < len(values)])
        for column in range(width)
    ]
    return [
        values + column_medians[len(values):]
        for _, values in rows
    ]


def temporal_median(matrix, window):
    radius = window // 2
    filtered = []
    for row_index in range(len(matrix)):
        start = max(0, row_index - radius)
        end = min(len(matrix), row_index + radius + 1)
        filtered.append([
            sorted(matrix[index][column] for index in range(start, end))[((end - start) - 1) // 2]
            for column in range(len(matrix[0]))
        ])
    return filtered


def moving_average(matrix, window):
    radius = window // 2
    filtered = []
    for row_index in range(len(matrix)):
        start = max(0, row_index - radius)
        end = min(len(matrix), row_index + radius + 1)
        filtered.append([
            sum(matrix[index][column] for index in range(start, end)) / (end - start)
            for column in range(len(matrix[0]))
        ])
    return filtered


def process(input_csv, output_csv, window):
    rows = parse_rows(input_csv)
    raw_matrix = pad_rows(rows)
    median_matrix = temporal_median(raw_matrix, window)
    filtered_matrix = moving_average(median_matrix, window)

    output_csv.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "frame", "id", "mac", "rssi", "source_len", "filtered_data",
        "mean_amplitude", "std_amplitude", "max_amplitude",
    ]
    with output_csv.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        writer.writeheader()
        for index, (row, _) in enumerate(rows):
            source_len = len(rows[index][1])
            filtered_values = filtered_matrix[index][:source_len]
            amplitudes = [
                math.hypot(filtered_values[offset], filtered_values[offset + 1])
                for offset in range(0, len(filtered_values) - 1, 2)
            ]
            mean_amplitude = sum(amplitudes) / len(amplitudes)
            writer.writerow({
                "frame": index,
                "id": row.get("id", index),
                "mac": row.get("mac", ""),
                "rssi": row.get("rssi", ""),
                "source_len": len(filtered_values),
                "filtered_data": json.dumps([round(value, 4) for value in filtered_values]),
                "mean_amplitude": round(mean_amplitude, 6),
                "std_amplitude": round(
                    math.sqrt(sum((value - mean_amplitude) ** 2 for value in amplitudes) / len(amplitudes)),
                    6,
                ),
                "max_amplitude": round(max(amplitudes), 6),
            })
    return len(rows)


def main():
    args = parse_args()
    try:
        window = odd_window(args.window)
        output = args.output or args.input_csv.with_name(
            f"{args.input_csv.stem}.filtered.csv"
        )
        count = process(args.input_csv, output, window)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(f"CSI processing failed: {error}") from error
    print(f"Processed {count} CSI frames")
    print(f"Filtered output: {output}")


if __name__ == "__main__":
    main()
