"""Run a labeled CSI burst and save raw samples plus metadata."""

import argparse
import csv
import json
import time
from datetime import datetime, timezone
from pathlib import Path

import serial


LEGACY_HEADERS = [
    "type", "id", "mac", "rssi", "rate", "sig_mode", "mcs", "bandwidth",
    "smoothing", "not_sounding", "aggregation", "stbc", "fec_coding", "sgi",
    "noise_floor", "ampdu_cnt", "channel", "secondary_channel", "local_timestamp",
    "ant", "sig_len", "rx_state", "len", "first_word", "data",
]
C5C6_HEADERS = [
    "type", "id", "mac", "rssi", "rate", "noise_floor", "fft_gain", "agc_gain",
    "channel", "local_timestamp", "sig_len", "rx_state", "len", "first_word", "data",
]


def parse_args():
    parser = argparse.ArgumentParser(description="Collect one labeled CSI burst")
    parser.add_argument("--transmitter", required=True, help="Transmitter serial port, for example COM3")
    parser.add_argument("--receiver", required=True, help="Receiver serial port, for example COM4")
    parser.add_argument("--label", choices=("empty", "metal", "non_metal"), required=True)
    parser.add_argument("--duration-ms", type=int, default=5000)
    parser.add_argument("--rate-hz", type=int, default=100)
    parser.add_argument("--output-dir", default="data")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--run-id", default=None, help="Optional name such as metal_001")
    return parser.parse_args()


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def header_for(fields):
    if len(fields) == len(LEGACY_HEADERS):
        return LEGACY_HEADERS
    if len(fields) == len(C5C6_HEADERS):
        return C5C6_HEADERS
    return [f"field_{index}" for index in range(len(fields))]


def collect(args):
    if args.duration_ms < 1 or args.rate_hz < 1:
        raise ValueError("duration-ms and rate-hz must be positive")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    run_id = args.run_id or f"{args.label}_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    csv_path = output_dir / f"{run_id}.csv"
    metadata_path = output_dir / f"{run_id}.json"
    metadata = {
        "schema_version": 1,
        "run_id": run_id,
        "label": args.label,
        "duration_ms": args.duration_ms,
        "rate_hz": args.rate_hz,
        "channel": 11,
        "transmitter_port": args.transmitter,
        "receiver_port": args.receiver,
        "baud": args.baud,
        "started_at": utc_now(),
    }

    valid_count = 0
    log_lines = []
    header = None
    burst_end_seen = False
    deadline = time.monotonic() + args.duration_ms / 1000 + 3

    with serial.Serial(args.transmitter, args.baud, timeout=0.05) as transmitter, serial.Serial(
        args.receiver, args.baud, timeout=0.05
    ) as receiver, csv_path.open("w", newline="", encoding="utf-8") as csv_file:
        transmitter.reset_input_buffer()
        receiver.reset_input_buffer()
        writer = csv.writer(csv_file)
        command = f"START {args.duration_ms} {args.rate_hz}\n".encode("ascii")
        transmitter.write(command)
        transmitter.flush()
        metadata["command_sent_at"] = utc_now()

        while time.monotonic() < deadline:
            tx_line = transmitter.readline()
            if tx_line:
                text = tx_line.decode("utf-8", errors="replace").strip()
                if text:
                    log_lines.append(text)
                    if text.startswith("BURST_END"):
                        burst_end_seen = True

            rx_line = receiver.readline()
            if not rx_line:
                if burst_end_seen:
                    break
                continue

            text = rx_line.decode("utf-8", errors="replace").strip()
            if not text:
                continue
            if text.startswith("type,"):
                header = next(csv.reader([text]))
                writer.writerow(header)
                continue
            if text.startswith("CSI_DATA,"):
                fields = next(csv.reader([text]))
                if header is None:
                    header = header_for(fields)
                    writer.writerow(header)
                writer.writerow(fields)
                valid_count += 1
            else:
                log_lines.append(text)

        if not burst_end_seen:
            transmitter.write(b"STOP\n")
            transmitter.flush()

    metadata["finished_at"] = utc_now()
    metadata["samples"] = valid_count
    metadata["transmitter_log"] = log_lines
    metadata["complete"] = burst_end_seen
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(f"Saved {valid_count} CSI samples to {csv_path}")
    print(f"Saved metadata to {metadata_path}")
    if not burst_end_seen:
        print("Warning: transmitter burst end was not observed")


def main():
    try:
        collect(parse_args())
    except (OSError, serial.SerialException, ValueError) as error:
        raise SystemExit(f"Collection failed: {error}") from error


if __name__ == "__main__":
    main()
