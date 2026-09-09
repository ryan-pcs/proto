import serial
import time


def read_available(port, seconds=1):
    data = b""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        data += port.read(4096)
    return data.decode("utf-8", "replace")


tx = serial.Serial("COM3", 115200, timeout=0.1)
rx = serial.Serial("COM4", 921600, timeout=0.1)
try:
    time.sleep(2)
    tx.reset_input_buffer()
    print("RECEIVER_BEFORE")
    print(read_available(rx))
    rx.reset_input_buffer()
    tx.write(b"START 5000 10\n")
    tx.flush()
    started = time.monotonic()
    time.sleep(6)
    print("TRANSMITTER")
    print(read_available(tx))
    receiver_output = read_available(rx)
    print("RECEIVER")
    print(receiver_output[:20000])
    print("ESP_NOW_RX_COUNT", receiver_output.count("ESP_NOW_RX"))
    print("CSI_DATA_COUNT", receiver_output.count("CSI_DATA,"))
finally:
    tx.close()
    rx.close()
