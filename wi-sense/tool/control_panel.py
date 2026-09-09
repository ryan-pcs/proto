"""Simple interactive terminal panel for the CSI transmitter."""

from control_transmitter import MAX_RATE_HZ, MAX_SECONDS, send_command

import serial
from serial.tools import list_ports


LABELS = ("empty", "metal", "non_metal")


def show_ports():
    devices = list(list_ports.comports())
    if not devices:
        print("No serial ports found.")
        return
    for device in devices:
        print(f"{device.device}: {device.description}")


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


def choose_label():
    print("1. empty\n2. metal\n3. non_metal")
    while True:
        choice = input("Label [1]: ").strip() or "1"
        if choice in {"1", "2", "3"}:
            return LABELS[int(choice) - 1]
        print("Choose 1, 2, or 3.")


def run_panel():
    show_ports()
    while True:
        port = input("Enter transmitter port only, for example COM3 [COM3]: ").strip() or "COM3"
        if " " not in port and port.upper().startswith("COM"):
            port = port.upper()
            break
        print("This prompt needs a port such as COM3. Do not enter START here.")
    baud = 115200

    while True:
        print("\n=== Wi-Sense Transmitter ===")
        print(f"Port: {port}   Baud: {baud}")
        print("1. Check status")
        print("2. Start burst (time, rate, and label)")
        print("3. Stop burst")
        print("4. List ports")
        print("5. Change transmitter port")
        print("0. Exit")
        choice = input("Choose [1]: ").strip() or "1"

        if choice == "0":
            return
        if choice == "1":
            with serial.Serial(port, baud, timeout=0.2) as connection:
                send_command(connection, "STATUS", 2)
        elif choice == "2":
            seconds = ask_number("Duration in seconds", 0.1, MAX_SECONDS, 5, float)
            rate = ask_number("Packets per second", 1, MAX_RATE_HZ, 50, int)
            label = choose_label()
            print(f"Starting {label}: {seconds:g} seconds at {rate} packets/sec")
            with serial.Serial(port, baud, timeout=0.2) as connection:
                send_command(connection, f"START {round(seconds * 1000)} {rate}", seconds + 5)
        elif choice == "3":
            with serial.Serial(port, baud, timeout=0.2) as connection:
                send_command(connection, "STOP", 2)
        elif choice == "4":
            show_ports()
        elif choice == "5":
            port = input(f"Transmitter port [{port}]: ").strip() or port
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
