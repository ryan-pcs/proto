"""Run a labeled CSI burst and save raw samples plus metadata."""

import argparse
import csv
import json
import math
import re
import time
from datetime import datetime, timezone
from pathlib import Path
import serial

from extract_features import extract
from process_csi import process

import serial


LEGACY_HEADERS = [
    "type", "id", "mac", "rssi", "rate", "sig_mode", "mcs", "bandwidth",
    "smoothing", "not_sounding", "aggregation", "stbc", "fec_coding", "sgi",
    "noise_floor", "ampdu_cnt", "channel", "secondary_channel", "local_timestamp",
    "ant", "sig_len", "sig_mode", "len", "first_word", "data",
]
C5C6_HEADERS = [
    "type", "id", "mac", "rssi", "rate", "noise_floor", "fft_gain", "agc_gain",
    "channel", "local_timestamp", "sig_len", "rx_format", "len", "first_word", "data",
]
MAX_DURATION_MS = 60000
MAX_RATE_HZ = 1000
MIN_CSI_COVERAGE_RATIO = 0.75
RECEIVER_STARTUP_TIMEOUT_SECONDS = 20
RECEIVER_CAPTURE_COMMAND_TIMEOUT_SECONDS = 3


def safe_component(value, fallback):
    component = re.sub(r"[^A-Za-z0-9_-]+", "_", str(value)).strip("_-")
    return component or fallback


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


def valid_csi_fields(fields, header):
    if header is None:
        return False
    if header is not None and len(fields) != len(header):
        return False
    if len(fields) not in (len(LEGACY_HEADERS), len(C5C6_HEADERS)):
        return False
    try:
        declared_length = int(fields[-3])
        values = json.loads(fields[-1])
        numeric_values = [float(value) for value in values]
    except (TypeError, ValueError, json.JSONDecodeError):
        return False
    return (
        declared_length == len(numeric_values)
        and declared_length >= 2
        and all(math.isfinite(value) for value in numeric_values)
    )


def consume_receiver_lines(lines, header, writer, receiver_log):
    valid_count = 0
    for text in lines:
        if text.startswith("type,"):
            candidate_header = next(csv.reader([text]))
            expected_header = header_for([""] * len(candidate_header))
            if candidate_header != expected_header:
                receiver_log.append(f"INVALID_CSI_HEADER,{text}")
                continue
            header = candidate_header
            writer.writerow(header)
            continue
        if text.startswith("CSI_DATA,"):
            fields = next(csv.reader([text]))
            if not valid_csi_fields(fields, header):
                receiver_log.append(f"INVALID_CSI_DATA,{text}")
                continue
            writer.writerow(fields)
            valid_count += 1
        else:
            receiver_log.append(text)
    return header, valid_count


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
        saw_status = False
        deadline = time.monotonic() + RECEIVER_STARTUP_TIMEOUT_SECONDS
        next_progress = time.monotonic() + 2
        next_status_probe = time.monotonic()
        while time.monotonic() < deadline:
            if time.monotonic() >= next_status_probe:
                receiver.write(b"CSI_STATUS\n")
                receiver.flush()
                next_status_probe = time.monotonic() + 1
            chunk = receiver.read(min(receiver.in_waiting, 1024) or 1)
            if chunk:
                startup += chunk
            saw_connected = saw_connected or b"WiFi connected to CSI_TX" in startup
            saw_ready = saw_ready or b"CSI ready" in startup
            saw_heartbeat = saw_heartbeat or b"RECEIVER_READY,ssid=CSI_TX,csi=enabled,wifi=connected" in startup
            saw_status = saw_status or b"RECEIVER_STATUS,ready=1,wifi=1" in startup
            if saw_status or saw_heartbeat or (saw_connected and saw_ready):
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


def arm_receiver(receiver):
    receiver.write(b"CSI_CAPTURE_ON\n")
    receiver.flush()
    buffer = b""
    deadline = time.monotonic() + RECEIVER_CAPTURE_COMMAND_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        lines, buffer = read_complete_lines(receiver, buffer)
        if any(line == "CSI_CAPTURE_READY" for line in lines):
            receiver.reset_input_buffer()
            return
    raise RuntimeError("receiver did not arm CSI capture")


def disarm_receiver(receiver, receiver_buffer, header, writer, receiver_log):
    receiver.write(b"CSI_CAPTURE_OFF\n")
    receiver.flush()
    buffer = b""
    deadline = time.monotonic() + RECEIVER_CAPTURE_COMMAND_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        lines, buffer = read_complete_lines(receiver, buffer)
        header, drained_count = consume_receiver_lines(lines, header, writer, receiver_log)
        for line in lines:
            if line.startswith("CSI_CAPTURE_STOPPED"):
                fields = {
                    part.split("=", 1)[0]: part.split("=", 1)[1]
                    for part in line.split(",")[1:]
                    if "=" in part
                }
                return int(fields.get("drops", 0)), header, drained_count
            if line.startswith("CSI_CAPTURE_STOP_TIMEOUT"):
                raise RuntimeError("receiver did not drain CSI output")
    raise RuntimeError("receiver did not confirm CSI capture stop")


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
        "receiver_capture_armed": False,
        "started_at": utc_now(),
    }

    valid_count = 0
    log_lines = []
    header = None
    burst_end_seen = False
    receiver_connected = False
    receiver_log = []
    deadline = None
    receiver_capture_stopped = False
    receiver_queue_drops = 0
    drained_count = 0

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
        try:
            arm_receiver(receiver)
        except (OSError, serial.SerialException, RuntimeError):
            receiver.close()
            raise
        metadata["receiver_capture_armed"] = True
        try:
            receiver.reset_input_buffer()
            command = f"START {args.duration_ms} {args.rate_hz}\n".encode("ascii")
            transmitter.write(command)
            transmitter.flush()
            metadata["command_sent_at"] = utc_now()
        except (OSError, serial.SerialException):
            receiver.close()
            raise
        capture_seconds = args.duration_ms / 1000
        deadline = time.monotonic() + capture_seconds + 2
        progress_at = time.monotonic()
        progress_chars = "|/-\\"
        progress_index = 0
        transmitter_buffer = b""
        receiver_buffer = b""

        try:
            while deadline is not None and time.monotonic() < deadline:
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
                        candidate_header = next(csv.reader([text]))
                        expected_header = header_for([""] * len(candidate_header))
                        if candidate_header != expected_header:
                            receiver_log.append(f"INVALID_CSI_HEADER,{text}")
                            continue
                        header = candidate_header
                        writer.writerow(header)
                        continue
                    if text.startswith("CSI_DATA,"):
                        fields = next(csv.reader([text]))
                        if not valid_csi_fields(fields, header):
                            receiver_log.append(f"INVALID_CSI_DATA,{text}")
                            continue
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
        finally:
            if not burst_end_seen:
                try:
                    transmitter.write(b"STOP\n")
                    transmitter.flush()
                except (OSError, serial.SerialException):
                    pass
            try:
                receiver_queue_drops, header, drained_count = disarm_receiver(
                    receiver, receiver_buffer, header, writer, receiver_log
                )
                receiver_capture_stopped = True
            except (OSError, serial.SerialException, RuntimeError):
                pass
            receiver.close()

    metadata["finished_at"] = utc_now()
    valid_count += drained_count
    metadata["samples"] = valid_count
    metadata["transmitter_log"] = log_lines
    metadata["receiver_connected"] = receiver_connected
    metadata["receiver_log"] = receiver_log
    metadata["receiver_capture_stopped"] = receiver_capture_stopped
    metadata["receiver_queue_drops"] = receiver_queue_drops
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
    metadata["sample_coverage_ratio"] = (
        round(valid_count / transmitter_packets, 6) if transmitter_packets > 0 else 0.0
    )
    metadata["sample_coverage_acceptable"] = (
        transmitter_packets > 0
        and metadata["sample_coverage_ratio"] >= MIN_CSI_COVERAGE_RATIO
    )
    # Retained for compatibility; CSI callbacks are not one-to-one UDP acknowledgements.
    metadata["sample_count_matches_transmitter"] = (
        transmitter_packets > 0 and valid_count == transmitter_packets
    )
    metadata["processing_status"] = "not_run"
    metadata["derived_files"] = []
    if valid_count == 0:
        metadata["capture_status"] = "no_csi_received"
    elif (
        not burst_end_seen
        or transmitter_failures > 0
        or not receiver_capture_stopped
        or receiver_queue_drops > 0
        or not metadata["sample_coverage_acceptable"]
    ):
        metadata["capture_status"] = "partial"
    else:
        metadata["capture_status"] = "success"
        if metadata["capture_status"] == "success":
            filtered_path = csv_path.with_name(f"{csv_path.stem}.filtered.csv")
            features_path = filtered_path.with_name(f"{filtered_path.stem}.features.json")
            try:
                process(csv_path, filtered_path, 3)
                extract(filtered_path, features_path)
                metadata["processing_status"] = "success"
                metadata["derived_files"] = [str(filtered_path), str(features_path)]
            except (OSError, ValueError, json.JSONDecodeError) as error:
                metadata["processing_status"] = "failed"
                metadata["processing_error"] = str(error)
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
        f"Processing status: {metadata['processing_status']}",
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
