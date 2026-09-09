# Empty Run Archive

This folder contains failed or empty collection attempts moved out of the active dataset.

Keep CSV and matching JSON files together. Do not use archived runs for training.
A run is valid only when its JSON has `samples` greater than zero,
`raw_data_stored` set to `true`, and `capture_status` set to `success`.
