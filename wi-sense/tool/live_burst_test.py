import serial
import time

TX_PORT = "COM3"
RX_PORT = "COM4"
BAUD = 115200


def read_lines(port, count=20):
    lines = []
    for _ in range(count):
        line = port.readline()
        if not line:
            continue
        text = line.decode("utf-8", "replace").strip()
        if text:
            lines.append(text)
    return lines


def read_for(port, seconds):
    deadline = time.monotonic() + seconds
    buffer = b""
    count = 0
    samples = []
    extras = []
    while time.monotonic() < deadline:
        chunk = port.read(min(port.in_waiting, 1024) or 1)
        if not chunk:
            continue
        buffer += chunk
        while b"\n" in buffer:
            raw_line, buffer = buffer.split(b"\n", 1)
            text = raw_line.decode("utf-8", "replace").strip()
            if text.startswith("CSI_DATA,"):
                count += 1
                if len(samples) < 3:
                    samples.append(text)
            elif text and len(extras) < 5:
                extras.append(text)
    return count, samples, extras


tx = serial.Serial(TX_PORT, BAUD, timeout=0.2)
rx = serial.Serial(RX_PORT, BAUD, timeout=0.5)

try:
    time.sleep(1)
    tx.reset_input_buffer()
    rx.reset_input_buffer()

    print("TX_STATUS")
    tx.write(b"STATUS\n")
    tx.flush()
    time.sleep(0.5)
    print("\n".join(read_lines(tx, 10)))

    print("START_BURST")
    tx.write(b"START 5000 50\n")
    tx.flush()
    time.sleep(0.5)
    print("\n".join(read_lines(tx, 20)))

    print("COLLECTING", flush=True)
    count, samples, extras = read_for(rx, 8)

    print(f"CSI_COUNT {count}")
    print(f"SAMPLE {samples[:3]}")
    print(f"NON_CSI {extras[:5]}")

    tx.write(b"STOP\n")
    tx.flush()
    print("STOP_REPLY")
    print("\n".join(read_lines(tx, 6)))
finally:
    tx.close()
    rx.close()
