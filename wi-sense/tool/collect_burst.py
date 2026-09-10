"""Run a labeled CSI burst and save raw samples plus metadata."""

import argparse
import csv
import json
import os
import re
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
MAX_DURATION_MS = 60000
MAX_RATE_HZ = 1000
RECEIVER_STARTUP_TIMEOUT_SECONDS = 20
RECEIVER_SETTLE_SECONDS = 2


def safe_component(value, fallback):
    component = re.sub(r"[^A-Za-z0-9_-]+", "_", str(value)).strip("_-")
    return component or fallback


def cancel_requested():
    if os.name != "nt":
        return False
    import msvcrt

    requested = False
    while msvcrt.kbhit():
        key = msvcrt.getwch().lower()
        requested = requested or key == "q"
    return requested


def parse_args():
    parser = argparse.ArgumentParser(description="Collect one labeled CSI burst")
    parser.add_argument("--transmitter", required=True, help="Transmitter serial port, for example COM3")
    parser.add_argument("--receiver", required=True, help="Receiver serial port, for example COM4")
    parser.add_argument("--label", required=True, help="Category; normally the selected data folder")
    parser.add_argument("--object-name", default="unnamed", help="Object name or empty-environment identifier")
    parser.add_argument("--duration-ms", type=int, default=5000)
    parser.add_argument("--rate-hz", type=int, default=100)
    parser.add_argument("--output-dir", default="data")
    parser.add_argument("--baud", type=int, default=115200, help="Transmitter baud rate")
    parser.add_argument("--receiver-baud", default="auto", help="Receiver baud rate or auto")
    parser.add_argument("--run-id", default=None, help="Data folder name; defaults to capture")
    return parser.parse_args()


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def header_for(fields):
    if len(fields) == len(LEGACY_HEADERS):
        return LEGACY_HEADERS
    if len(fields) == len(C5C6_HEADERS):
        return C5C6_HEADERS
    return [f"field_{index}" for index in range(len(fields))]


def read_complete_lines(port, buffer):
    chunk = port.read(min(port.in_waiting, 1024) or 1)
    if chunk:
        buffer += chunk
    lines = []
    while b"\n" in buffer:
        raw_line, buffer = buffer.split(b"\n", 1)
        text = raw_line.decode("utf-8", errors="replace").strip()
        if text:
            lines.append(text)
    return lines, buffer


def open_receiver(port_name, requested_baud):
    baud_candidates = [921600, 115200] if requested_baud == "auto" else [int(requested_baud)]
    attempts = []
    for baud in baud_candidates:
        print(f"Preparing receiver {port_name} at {baud} baud", flush=True)
        receiver = serial.Serial(port_name, baud, timeout=0.05)
        startup = b""
        saw_connected = False
        saw_ready = False
        saw_heartbeat = False
        deadline = time.monotonic() + RECEIVER_STARTUP_TIMEOUT_SECONDS
        next_progress = time.monotonic() + 2
        while time.monotonic() < deadline:
            chunk = receiver.read(min(receiver.in_waiting, 1024) or 1)
            if chunk:
                startup += chunk
            saw_connected = saw_connected or b"WiFi connected to CSI_TX" in startup
            saw_ready = saw_ready or b"CSI ready" in startup
            saw_heartbeat = saw_heartbeat or b"RECEIVER_READY,ssid=CSI_TX,csi=enabled,wifi=connected" in startup
            if saw_heartbeat or (saw_connected and saw_ready):
                receiver.reset_input_buffer()
                print("Receiver ready.", flush=True)
                return receiver, baud
            if time.monotonic() >= next_progress:
                print("Waiting for receiver readiness...", flush=True)
                next_progress += 2
        receiver.close()
        attempts.append(
            f"{baud}: connected={saw_connected}, csi_ready={saw_ready}, heartbeat={saw_heartbeat}"
        )
    raise RuntimeError(
        f"receiver did not become ready on {port_name}; observed {'; '.join(attempts)}"
    )


def collect(args):
    if not 1 <= args.duration_ms <= MAX_DURATION_MS:
        raise ValueError(f"duration-ms must be between 1 and {MAX_DURATION_MS}")
    if not 1 <= args.rate_hz <= MAX_RATE_HZ:
        raise ValueError(f"rate-hz must be between 1 and {MAX_RATE_HZ}")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%m-%d-%Y_%H%M%S")
    folder_name = safe_component(args.run_id or "capture", "capture")
    duration_name = f"{args.duration_ms / 1000:g}s"
    frequency_name = f"{args.rate_hz}hz"
    object_name = safe_component(args.object_name, "unnamed")
    label = safe_component(args.label, "other")
    run_id = f"{object_name}_{label}_{duration_name}_{frequency_name}_{timestamp}"
    run_dir = output_dir / folder_name
    run_dir.mkdir(parents=True, exist_ok=True)
    file_stem = f"{object_name}_{label}_{duration_name}_{frequency_name}_{timestamp}"
    csv_path = run_dir / f"{file_stem}.csv"
    metadata_path = run_dir / f"{file_stem}.json"
    metadata = {
        "schema_version": 2,
        "run_id": run_id,
        "category": label,
        "object_name": object_name,
        "description": {
            "empty": "empty environment",
            "metal": "metal object",
            "non_metal": "non-metal object",
        }.get(label, "custom category"),
        "duration_ms": args.duration_ms,
        "rate_hz": args.rate_hz,
        "channel": 11,
        "transmitter_port": args.transmitter,
        "receiver_port": args.receiver,
        "transmitter_baud": args.baud,
        "receiver_baud": args.receiver_baud,
        "started_at": utc_now(),
    }

    valid_count = 0
    log_lines = []
    header = None
    burst_end_seen = False
    receiver_connected = False
    receiver_log = []
    deadline = None
    cancelled = False

    receiver = None
    with serial.Serial(args.transmitter, args.baud, timeout=0.05) as transmitter, csv_path.open(
        "w", newline="", encoding="utf-8"
    ) as csv_file:
        time.sleep(1)
        transmitter.reset_input_buffer()
        receiver, actual_receiver_baud = open_receiver(args.receiver, args.receiver_baud)
        writer = csv.writer(csv_file)

        receiver_connected = True
        metadata["receiver_baud"] = actual_receiver_baud
        time.sleep(RECEIVER_SETTLE_SECONDS)

        command = f"START {args.duration_ms} {args.rate_hz}\n".encode("ascii")
        transmitter.write(command)
        transmitter.flush()
        metadata["command_sent_at"] = utc_now()
        capture_seconds = args.duration_ms / 1000
        deadline = time.monotonic() + capture_seconds + 2
        progress_at = time.monotonic()
        progress_chars = "|/-\\"
        progress_index = 0
        transmitter_buffer = b""
        receiver_buffer = b""

        while deadline is not None and time.monotonic() < deadline:
            if cancel_requested():
                cancelled = True
                print("\nCancel requested; stopping burst and discarding capture.", flush=True)
                break
            now = time.monotonic()
            if now >= progress_at:
                elapsed = min(now - (deadline - capture_seconds - 2), capture_seconds)
                print(
                    f"\rCollecting CSI {progress_chars[progress_index % len(progress_chars)]} "
                    f"{elapsed:4.1f}/{capture_seconds:.1f}s | samples={valid_count}",
                    end="",
                    flush=True,
                )
                progress_index += 1
                progress_at = now + 0.5

            tx_lines, transmitter_buffer = read_complete_lines(transmitter, transmitter_buffer)
            for text in tx_lines:
                log_lines.append(text)
                if text.startswith("BURST_END"):
                    burst_end_seen = True

            rx_lines, receiver_buffer = read_complete_lines(receiver, receiver_buffer)
            for text in rx_lines:
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
                    csv_file.flush()
                    valid_count += 1
                else:
                    receiver_log.append(text)

        if not burst_end_seen:
            transmitter.write(b"STOP\n")
            transmitter.flush()
            stop_deadline = time.monotonic() + 1
            while time.monotonic() < stop_deadline and not burst_end_seen:
                tx_lines, transmitter_buffer = read_complete_lines(transmitter, transmitter_buffer)
                for text in tx_lines:
                    log_lines.append(text)
                    if text.startswith("BURST_END"):
                        burst_end_seen = True
                time.sleep(0.01)
        print()
        receiver.close()

    if cancelled:
        csv_path.unlink(missing_ok=True)
        print("Collection cancelled. No CSV or JSON record was saved.")
        return

    metadata["finished_at"] = utc_now()
    metadata["samples"] = valid_count
    metadata["transmitter_log"] = log_lines
    metadata["receiver_connected"] = receiver_connected
    metadata["receiver_log"] = receiver_log
    transmitter_packets = 0
    transmitter_failures = 0
    for line in log_lines:
        if line.startswith(("BURST_END,", "BURST_STOPPED,")):
            fields = {
                part.split("=", 1)[0]: part.split("=", 1)[1]
                for part in line.split(",")[1:]
                if "=" in part
            }
            transmitter_packets = int(fields.get("packets", transmitter_packets))
            transmitter_failures = int(fields.get("fail", transmitter_failures))
    metadata["transmitter_packets"] = transmitter_packets
    metadata["transmitter_failures"] = transmitter_failures
    metadata["complete"] = burst_end_seen
    metadata["raw_data_stored"] = valid_count > 0
    if valid_count == 0:
        metadata["capture_status"] = "no_csi_received"
    elif not burst_end_seen or transmitter_failures > 0:
        metadata["capture_status"] = "partial"
    else:
        metadata["capture_status"] = "success"
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    failed_capture = metadata["capture_status"] != "success"
    if failed_capture:
        archive_dir = output_dir / "archive" / folder_name
        archive_dir.mkdir(parents=True, exist_ok=True)
        csv_path.replace(archive_dir / csv_path.name)
        metadata_path.replace(archive_dir / metadata_path.name)
    box_lines = [
        f"Category: {metadata['category']} ({metadata['description']})",
        f"Transmitter packets: {transmitter_packets}",
        f"Transmitter failures: {transmitter_failures}",
        f"Receiver CSI samples: {valid_count}",
        f"Burst complete: {'yes' if burst_end_seen else 'no'}",
        f"Capture status: {metadata['capture_status']}",
        f"CSV saved: {csv_path}" if not failed_capture else "Failed files archived: yes",
        f"Metadata saved: {metadata_path}" if not failed_capture else f"Archive: {archive_dir}",
    ]
    print("\n=== COLLECTION RESULT ===")
    print("\n".join(box_lines))
    if not burst_end_seen:
        print("Warning: transmitter burst end was not observed")
    if valid_count == 0:
        print("Warning: CSV contains no CSI samples; raw capture was not successful")


def main():
    try:
        collect(parse_args())
    except (OSError, serial.SerialException, RuntimeError, ValueError) as error:
        raise SystemExit(f"Collection failed: {error}") from error


if __name__ == "__main__":
    main()
