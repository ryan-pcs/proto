"""Capture receiver CSI_DATA lines into a CSV file."""

import argparse
import csv
import io
import time
from pathlib import Path

import serial


def parse_args():
    parser = argparse.ArgumentParser(description="Collect CSI_DATA from an ESP32 receiver")
    parser.add_argument("-p", "--port", required=True, help="Receiver serial port, for example COM3")
    parser.add_argument("-o", "--output", default="data/csi_collection.csv", help="CSV output path")
    parser.add_argument("-l", "--log", default=None, help="Log output path; defaults beside the CSV")
    parser.add_argument("--baud", type=int, default=115200, help="Receiver baud rate")
    return parser.parse_args()


def collect(port_name, output_path, log_path, baud_rate):
    output_path = Path(output_path)
    log_path = Path(log_path) if log_path is not None else Path(output_path).with_suffix(".log")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)

    valid_count = 0
    log_count = 0
    header = None

    print(f"Opening {port_name} at {baud_rate} baud")
    with serial.Serial(port_name, baudrate=baud_rate, timeout=1) as port:
        with output_path.open("w", newline="", encoding="utf-8") as csv_file:
            with log_path.open("w", encoding="utf-8") as log_file:
                writer = None
                print("Collecting CSI data. Press Ctrl+C to stop.")
                try:
                    while True:
                        raw = port.readline()
                        if not raw:
                            continue

                        line = raw.decode("utf-8", errors="replace").strip()
                        if not line:
                            continue

                        if line.startswith("type,"):
                            header = next(csv.reader(io.StringIO(line)))
                            writer = csv.writer(csv_file)
                            writer.writerow(header)
                            csv_file.flush()
                            continue

                        if not line.startswith("CSI_DATA,"):
                            log_file.write(line + "\n")
                            log_file.flush()
                            log_count += 1
                            continue

                        try:
                            fields = next(csv.reader(io.StringIO(line)))
                            if len(fields) < 2:
                                raise ValueError("missing CSI fields")
                            if writer is None:
                                writer = csv.writer(csv_file)
                            writer.writerow(fields)
                            csv_file.flush()
                            valid_count += 1
                            if valid_count % 100 == 0:
                                print(f"Collected {valid_count} CSI lines", flush=True)
                        except (csv.Error, ValueError) as error:
                            log_file.write(f"parse error: {error}: {line}\n")
                            log_file.flush()
                            log_count += 1
                except KeyboardInterrupt:
                    pass

    print(f"Saved {valid_count} CSI lines to {output_path}")
    print(f"Saved {log_count} log lines to {log_path}")


def main():
    args = parse_args()
    log_path = args.log if args.log is not None else Path(args.output).with_suffix(".log")
    try:
        collect(args.port, args.output, log_path, args.baud)
    except serial.SerialException as error:
        raise SystemExit(f"Could not open {args.port}: {error}") from error


if __name__ == "__main__":
    main()