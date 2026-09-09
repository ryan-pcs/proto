# Working Commands

These are the commands that work with the current transmitter firmware.

## Ports

```text
Transmitter: COM3
Receiver: COM4
Baud: 115200
```

Close serial monitors before using Python. A serial port can only be opened by one program at a time.

## Terminal controller

Run from the `tool` folder.

For the easiest control, start the interactive panel:

```powershell
python .\control_panel.py
```

The panel lets you choose the transmitter port, check status, start a burst,
choose `empty`, `metal`, or `non_metal`, stop a burst, and list ports. No
PlatformIO monitor is needed.

Check the transmitter:

```powershell
python .\control_transmitter.py --port COM3 status
```

Start a burst:

```powershell
python .\control_transmitter.py --port COM3 burst --seconds 5 --rate 50 --label empty
```

Stop a burst:

```powershell
python .\control_transmitter.py --port COM3 stop
```

The label is saved as metadata. It is not sent to the transmitter.

## Device commands

These are the raw commands sent to the transmitter:

```text
STATUS
START 5000 50
STOP
```

Meaning of `START`:

```text
START duration_in_milliseconds packets_per_second
```

Examples:

```text
START 1000 20   = 1 second, 20 packets per second
START 5000 50   = 5 seconds, 50 packets per second
START 10000 100 = 10 seconds, 100 packets per second
```

Allowed values:

- Duration: `1` to `10000` milliseconds.
- Rate: `1` to `1000` packets per second.

Expected response:

```text
BURST_START,duration_ms=5000,rate_hz=50
BURST_END,packets=250
```

## HTML viewer

1. Close PlatformIO monitors.
2. Open `tool/csi_viewer.html` in Chrome or Edge.
3. Connect the receiver and select `COM4`.
4. Connect the transmitter and select `COM3`.
5. Select `empty`, `metal`, or `non_metal`.
6. Set the time and packet rate.
7. Click **Start burst**.

The HTML viewer saves raw CSI CSV data and JSON metadata. The browser downloads files to its Downloads folder and also keeps a local browser copy.

## Labels

- `empty`: no test object is between the boards.
- `metal`: a metal object is between the boards.
- `non_metal`: a non-metal object is between the boards.

These labels describe a known test setup. They do not identify an object automatically.
