"""Terminal controller for the transmitter; commands map directly to a future LCD UI."""

import argparse
import time

import serial
from serial.tools import list_ports


MAX_SECONDS = 60
MAX_RATE_HZ = 1000


def parse_args():
    parser = argparse.ArgumentParser(description="Control a CSI transmitter burst")
    parser.add_argument("--port", help="Transmitter serial port, for example COM3")
    parser.add_argument("--baud", type=int, default=115200)
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("ports", help="List connected serial devices")

    burst = subparsers.add_parser("burst", help="Run a timed burst")
    burst.add_argument("--seconds", type=float, default=5)
    burst.add_argument("--rate", type=int, default=100)
    burst.add_argument("--label", choices=("empty", "metal", "non_metal"), default="unlabeled")

    subparsers.add_parser("status", help="Read transmitter status")
    subparsers.add_parser("stop", help="Stop the active burst")
    return parser.parse_args()


def send_command(port, command, timeout):
    port.reset_input_buffer()
    port.write((command + "\n").encode("ascii"))
    port.flush()
    deadline = time.monotonic() + timeout
    out = []
    while time.monotonic() < deadline:
        line = port.readline()
        if line:
            text = line.decode("utf-8", errors="replace").strip()
            if text:
                print(text, flush=True)
                out.append(text)
                if text.startswith(("BURST_END", "BURST_STOPPED", "STATUS,")):
                    return out
    return out


def main():
    args = parse_args()
    if args.command == "ports":
        for device in list_ports.comports():
            print(f"{device.device}: {device.description}")
        return
    if not args.port:
        raise SystemExit("--port is required; run 'ports' first to list connected devices")
    if args.command == "burst":
        if not 0 < args.seconds <= MAX_SECONDS:
            raise SystemExit(f"seconds must be between 0 and {MAX_SECONDS}")
        if not 0 < args.rate <= MAX_RATE_HZ:
            raise SystemExit(f"rate must be between 1 and {MAX_RATE_HZ}")
        duration_ms = round(args.seconds * 1000)
        command = f"START {duration_ms} {args.rate}"
        print(f"label={args.label} command={command}", flush=True)
        timeout = args.seconds + 5
    elif args.command == "status":
        command = "STATUS"
        timeout = 2
    else:
        command = "STOP"
        timeout = 2

    try:
        with serial.Serial(args.port, args.baud, timeout=0.2) as port:
            send_command(port, command, timeout)
    except serial.SerialException as error:
        raise SystemExit(f"Could not open {args.port}: {error}") from error


if __name__ == "__main__":
    main()
