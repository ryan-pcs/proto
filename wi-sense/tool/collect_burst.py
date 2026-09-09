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
MAX_DURATION_MS = 60000
MAX_RATE_HZ = 1000
RECEIVER_STARTUP_TIMEOUT_SECONDS = 20
RECEIVER_SETTLE_SECONDS = 2


def parse_args():
    parser = argparse.ArgumentParser(description="Collect one labeled CSI burst")
    parser.add_argument("--transmitter", required=True, help="Transmitter serial port, for example COM3")
    parser.add_argument("--receiver", required=True, help="Receiver serial port, for example COM4")
    parser.add_argument("--label", choices=("empty", "metal", "non_metal"), required=True)
    parser.add_argument("--duration-ms", type=int, default=5000)
    parser.add_argument("--rate-hz", type=int, default=100)
    parser.add_argument("--output-dir", default="data")
    parser.add_argument("--baud", type=int, default=115200, help="Transmitter baud rate")
    parser.add_argument("--receiver-baud", default="auto", help="Receiver baud rate or auto")
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
    for baud in baud_candidates:
        receiver = serial.Serial(port_name, baud, timeout=0.05)
        startup = b""
        saw_connected = False
        saw_ready = False
        deadline = time.monotonic() + RECEIVER_STARTUP_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            chunk = receiver.read(min(receiver.in_waiting, 1024) or 1)
            if chunk:
                startup += chunk
            saw_connected = saw_connected or b"WiFi connected to CSI_TX" in startup
            saw_ready = saw_ready or b"CSI ready" in startup
            if saw_connected and saw_ready:
                receiver.reset_input_buffer()
                return receiver, baud
        if baud == 921600:
            receiver.reset_input_buffer()
            return receiver, baud
        receiver.close()
    raise RuntimeError("receiver did not connect to CSI_TX at 921600 or 115200 baud")


def collect(args):
    if not 1 <= args.duration_ms <= MAX_DURATION_MS:
        raise ValueError(f"duration-ms must be between 1 and {MAX_DURATION_MS}")
    if not 1 <= args.rate_hz <= MAX_RATE_HZ:
        raise ValueError(f"rate-hz must be between 1 and {MAX_RATE_HZ}")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_id = f"{args.run_id or args.label}_{timestamp}"
    csv_path = output_dir / f"{run_id}.csv"
    metadata_path = output_dir / f"{run_id}.json"
    metadata = {
        "schema_version": 1,
        "run_id": run_id,
        "label": args.label,
        "classification": {
            "item": args.label,
            "category": args.label,
            "environment_label": args.label,
            "description": {
                "empty": "empty environment",
                "metal": "metal object",
                "non_metal": "non-metal object",
            }.get(args.label, "unknown")
        },
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
        print()
        receiver.close()

    metadata["finished_at"] = utc_now()
    metadata["samples"] = valid_count
    metadata["transmitter_log"] = log_lines
    metadata["receiver_connected"] = receiver_connected
    metadata["receiver_log"] = receiver_log
    transmitter_packets = 0
    for line in log_lines:
        if line.startswith("BURST_END,packets="):
            packet_field = line.split(",", 2)[1]
            transmitter_packets = int(packet_field.split("=", 1)[1])
    metadata["transmitter_packets"] = transmitter_packets
    metadata["complete"] = burst_end_seen
    metadata["raw_data_stored"] = valid_count > 0
    metadata["capture_status"] = "success" if valid_count > 0 else "no_csi_received"
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    failed_capture = valid_count == 0
    if failed_capture:
        csv_path.unlink(missing_ok=True)
        metadata_path.unlink(missing_ok=True)
    box_lines = [
        f"Label: {args.label} ({metadata['classification']['description']})",
        f"Transmitter packets: {transmitter_packets}",
        f"Receiver CSI samples: {valid_count}",
        f"Burst complete: {'yes' if burst_end_seen else 'no'}",
        f"Capture status: {metadata['capture_status']}",
        f"CSV saved: {csv_path}" if not failed_capture else "Failed files removed: yes",
        f"Metadata saved: {metadata_path}" if not failed_capture else "Metadata saved: no",
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
