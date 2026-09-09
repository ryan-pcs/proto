# CSI Data Collection

The repository does not currently contain captured CSI data. The Python viewer records it while it is connected to the receiver.

## Before collecting

- Use two compatible ESP32 boards: one transmitter and one receiver.
- Keep the transmitter and receiver on the same channel. The current firmware uses channel 11.
- Record the board roles, board models, COM ports, date, room, distance, and activity for each run.
- Choose a fixed distance and keep the receiver orientation consistent.

## Start a recording

From the `tool` directory, run the viewer with a unique output name:

```powershell
python .\csi_data_read_parse.py -p COM_RECEIVER -s .\data\standing_01.csv -l .\data\standing_01.log
```

Replace `COM_RECEIVER` and the run name. Create the `data` directory first if needed:

```powershell
New-Item -ItemType Directory -Force .\data
```

Leave the viewer running for a fixed duration, such as 30 seconds, then close it. Avoid changing the setup during a run.

## Suggested first runs

Use the same location and duration for every run:

1. `empty_01`: no person between the boards.
2. `standing_01`: one person standing still between the boards.
3. `walking_01`: one person walking through the path.
4. `sitting_01`: one person sitting in the path.

Start with two or three runs per label. More consistent runs are more useful than one very long recording.

## Validate a recording

After stopping the viewer, confirm that the CSV is not empty and contains CSI rows:

```powershell
Get-Item .\data\standing_01.csv
Select-String -Path .\data\standing_01.csv -Pattern CSI_DATA | Select-Object -First 3
```

Keep the matching `.log` file with the CSV. If the CSV has only a header or the log reports incomplete data, do not use that run as training data.

## Record the metadata

For each CSV, note:

- label and run number
- date and time
- board models and firmware target
- transmitter and receiver COM ports
- distance and orientation
- room/setup description
- activity and duration
- any interruptions or people moving outside the planned activity
