"""Interactive terminal panel for labeled CSI collection."""

from argparse import Namespace
import time
import re
import textwrap

from collect_burst import collect
from control_transmitter import MAX_RATE_HZ, MAX_SECONDS, send_command

import serial
from serial.tools import list_ports


LABELS = ("empty", "metal", "non_metal")
DATA_FOLDERS = ("empty", "metal", "non_metal", "other")
BOX_WIDTH = 62
CLASSIFICATION_TEXT = {
    "empty": "empty environment",
    "metal": "metal object",
    "non_metal": "non-metal object",
}


def show_ports():
    devices = list(list_ports.comports())
    if not devices:
        print("No serial ports found.")
        return
    print("Detected serial ports:")
    for device in devices:
        print(f"  {device.device}: {device.description}")


def ask_number(prompt, minimum, maximum, default, cast):
    while True:
        value = input(f"{prompt} [{default}]: ").strip()
        if not value:
            return default
        try:
            parsed = cast(value)
        except ValueError:
            print("Enter a number.")
            continue
        if minimum <= parsed <= maximum:
            return parsed
        print(f"Use a value from {minimum} to {maximum}.")


def choose_data_folder():
    print("Data folder:")
    for index, folder in enumerate(DATA_FOLDERS, start=1):
        print(f"  {index}. {folder}")
    print("  5. Create new folder")
    while True:
        choice = input("Choose folder [1]: ").strip() or "1"
        if choice in {"1", "2", "3", "4"}:
            return DATA_FOLDERS[int(choice) - 1]
        if choice == "5":
            while True:
                value = input("New data folder name: ").strip()
                safe_value = re.sub(r"[^A-Za-z0-9_-]+", "_", value).strip("_-")
                if safe_value and safe_value not in DATA_FOLDERS:
                    return safe_value
                print("Use a new folder name such as test_01.")
        else:
            print("Choose 1 through 5.")


def ask_object_name(label):
    default = "none" if label == "empty" else None
    while True:
        prompt = f"Object name [{default}]: " if default else "Object name: "
        value = input(prompt).strip()
        if not value and default:
            return default
        safe_value = re.sub(r"[^A-Za-z0-9_-]+", "_", value).strip("_-")
        if safe_value:
            return safe_value
        print("Enter a name such as mug, keys, or none.")


def box(title, lines):
    print("\n+" + "-" * BOX_WIDTH + "+")
    print(f"| {title:<{BOX_WIDTH - 2}} |")
    print("+" + "-" * BOX_WIDTH + "+")
    for line in lines:
        wrapped_lines = textwrap.wrap(
            str(line), width=BOX_WIDTH - 4, break_long_words=True, break_on_hyphens=False
        ) or [""]
        for wrapped_line in wrapped_lines:
            print(f"| {wrapped_line:<{BOX_WIDTH - 2}} |")
    print("+" + "-" * BOX_WIDTH + "+")


def ask_port(prompt, default):
    while True:
        value = input(f"{prompt} [{default}]: ").strip() or default
        value = value.upper()
        if " " not in value and value.startswith("COM"):
            return value
        print("Enter a port such as COM4.")


def detect_board_ports():
    candidates = [device.device for device in list_ports.comports() if device.device.upper() != "COM1"]
    transmitter = None
    for candidate in candidates:
        try:
            with serial.Serial(candidate, 115200, timeout=0.2) as connection:
                time.sleep(1)
                connection.reset_input_buffer()
                for _ in range(2):
                    connection.write(b"STATUS\n")
                    connection.flush()
                    deadline = time.monotonic() + 1
                    while time.monotonic() < deadline:
                        line = connection.readline()
                        if line.startswith(b"STATUS,"):
                            transmitter = candidate
                            break
                    if transmitter:
                        break
        except serial.SerialException:
            continue
        if transmitter:
            break

    receiver_candidates = [candidate for candidate in candidates if candidate != transmitter]
    receiver = receiver_candidates[0] if len(receiver_candidates) == 1 else None
    return transmitter, receiver


def collect_burst_from_panel(transmitter, receiver, seconds, rate, label, object_name, baud, folder_name):
    collect(Namespace(
        transmitter=transmitter,
        receiver=receiver,
        label=label,
        object_name=object_name,
        duration_ms=round(seconds * 1000),
        rate_hz=rate,
        output_dir="data",
        baud=baud,
        receiver_baud="auto",
        run_id=folder_name,
    ))


def run_panel():
    show_ports()
    print("Detecting boards by asking each serial port for STATUS...")
    port, receiver_port = detect_board_ports()
    if port and receiver_port:
        print(f"Detected transmitter: {port}")
        print(f"Detected receiver: {receiver_port}")
    else:
        print("Automatic detection was incomplete.")
        port = ask_port("Transmitter port", port or "COM3")
        receiver_port = ask_port("Receiver port", receiver_port or "COM4")
    baud = 115200

    while True:
        box("WI-SENSE CONTROL PANEL", [
            f"Transmitter: {port}    Receiver: {receiver_port}",
            "1  Check transmitter status",
            "2  Collect labeled CSI burst",
            "3  Stop transmitter burst (idle/manual)",
            "4  List serial ports",
            "5  Change board ports",
            "0  Exit",
        ])
        choice = input("Select [1]: ").strip() or "1"

        if choice == "0":
            return
        if choice == "1":
            with serial.Serial(port, baud, timeout=0.2) as connection:
                send_command(connection, "STATUS", 2)
        elif choice == "2":
            seconds = ask_number("Duration in seconds", 0.1, MAX_SECONDS, 5, float)
            rate = ask_number("Packets per second", 1, MAX_RATE_HZ, 50, int)
            folder_name = choose_data_folder()
            label = folder_name
            object_name = ask_object_name(label)
            box("BURST SETUP", [
                f"Category: {label} ({CLASSIFICATION_TEXT.get(label, 'custom category')})",
                f"Object: {object_name}",
                f"Folder: data/{folder_name}/",
                f"Duration: {seconds:g} seconds",
                f"Transmit rate: {rate} packets/second",
                f"Saving to: data/{folder_name}/{object_name}_{label}_<seconds>_<hz>_<date>.csv",
                "Close serial monitors on both ports before continuing.",
            ])
            input("Press Enter to start collection...")
            try:
                collect_burst_from_panel(port, receiver_port, seconds, rate, label, object_name, baud, folder_name)
            except (OSError, serial.SerialException, RuntimeError, ValueError) as error:
                box("COLLECTION FAILED", [str(error), "No trustworthy CSI result was recorded."])
        elif choice == "3":
            with serial.Serial(port, baud, timeout=0.2) as connection:
                send_command(connection, "STOP", 2)
        elif choice == "4":
            show_ports()
        elif choice == "5":
            port = ask_port("Transmitter port", port)
            receiver_port = ask_port("Receiver port", receiver_port)
        else:
            print("Choose a number from 0 to 5.")


def main():
    try:
        run_panel()
    except (KeyboardInterrupt, EOFError):
        print("\nPanel closed.")
    except serial.SerialException as error:
        print(f"Serial error: {error}")
        print("Close any serial monitor using the transmitter port and try again.")


if __name__ == "__main__":
    main()
