# Wi-Sense Repository — Knowledge Base

*Last updated: 2026-09-11. This is a living document — every session we work on this project, we add to it rather than starting over.*

## 1. Where it lives

Your student's code is on your computer at:
`Documents\GitHub\proto\wi-sense`

(The folder is named `proto`, and the actual project is the `wi-sense` folder inside it.)

## 2. What this project actually is, right now

The student has built a **working data-collection system**, not yet a working detector. In plain terms: the hardware can currently *capture* WiFi signal readings and save them to labeled files — but nothing yet looks at those files and says "this is metal" or "this is not metal." That second part (the actual detector) hasn't been built.

Two ESP32 boards (small WiFi-capable microcontrollers) talk to each other:

- **Transmitter board**: creates its own WiFi network (`CSI_TX`) and continuously sends small "ping" packets.
- **Receiver board**: connects to that network, listens to every ping, and reports the raw **CSI** — Channel State Information, i.e. how the WiFi signal got distorted on its way from transmitter to receiver (this is the "signature" the whole project depends on — an object between the two boards changes this signature).

A Python program on your PC (`control_panel.py`) talks to both boards over USB, tells them when to start/stop a recording run, and saves the result as a labeled file.

## 3. How the code folder is organized

Think of the repository (the whole project folder that GitHub Desktop is tracking — analogous to VMAS's master folder for one exam, holding everything about it in one place: this is an analogy) as three separate sub-programs plus documentation:

- `receiver/` — the code that runs *on* the receiver ESP32 board.
- `transmitter/` — the code that runs *on* the transmitter ESP32 board.
- `tool/` — Python scripts that run on your PC: collecting data, cleaning it, turning it into features, and (eventually) training a detector.
- `tool/data/` — currently empty (just a placeholder file) — this is where controlled `metal`/`non_metal`/`empty` recordings are supposed to go once collected.
- Four documentation files the student already wrote, at the top of `wi-sense/`:
  - `PROJECT_GUIDE.md` — the main reference: current status, hardware setup, how to run a collection, the workflow.
  - `COMMANDS.md` — exact commands to run, step by step.
  - `STORAGE.md` — the file/folder naming rules and what each saved file must contain to count as valid.
  - `CHANGELOG.md` — a dated log of every fix and finding (like a teacher's log of every correction made to an exam item, with the reason — an analogy).

**Note:** the student already built the knowledge base you asked for, inside the repository itself. These four files are thorough and well-maintained. My summary below is a condensed version of them for quick reference — the source files are the ground truth.

## 4. Current status (as the student's own notes state it)

**Working:**
- Transmitter creates its WiFi network and sends packets.
- Receiver connects and reports raw CSI over USB at high speed.
- The PC collection tool saves labeled runs correctly.
- Offline cleanup ("filtering") and feature extraction tools exist.
- A "dataset readiness" checker and a guarded training script exist.

**Not built yet:**
- No detector ("classifier" — the actual metal / non-metal decision-maker) has been trained.
- No controlled, clean `metal` / `non_metal` recordings exist yet — only test scraps under an `other` label (Tagalog test words like "wala", "nothing" — clearly just connection tests, not real data).
- The small screen ("TFT") that would eventually show results stand-alone isn't wired in yet.
- Running the detector directly on the receiver board (instead of a PC) isn't built.
- Saving to an SD card isn't built.

**Bottom line:** the plumbing works; the actual "is this metal" intelligence does not exist yet, and the student's own docs are explicit that no result should ever be labeled "metal detected" until a model is trained and tested.

## 5. Hardware setup

- Transmitter: USB port label can change, but was most recently `COM6`.
- Receiver: most recently `COM3` — but the student's notes warn these numbers shift every time you reconnect, so always double-check in the tool before collecting.
- Only one program can "own" a USB port at a time — close any serial monitor before running the Python tool.
- WiFi network created by the transmitter: name `CSI_TX`, password `csi12345`, on channel 11.

## 6. How a data-collection run works (the workflow)

1. Run `tool/control_panel.py` on the PC.
2. Pick a label for what's being recorded: `empty`, `metal`, `non_metal`, `other`, or a custom name.
3. Name the object (or `none` if nothing's there).
4. Set how long to record and how fast to sample.
5. The tool saves a `.csv` (the raw signal readings) and a matching `.json` (the "answer key" for that run — what was recorded, for how long, whether it succeeded).
6. Cleaning ("filtering") and feature extraction happen automatically right after.
7. Only runs that pass strict quality checks (explained below) count toward training a detector later.

**Quality bar for a run to be usable ("valid") — every one of these must hold:**
- At least 75% of the sent packets were actually captured as CSI (this threshold has been raised over time as the student found and fixed bugs).
- The run finished cleanly, not interrupted.
- Zero readings were dropped due to the receiver's internal buffer filling up.
- The saved answer-key file (JSON) matches the actual data file (CSV).

## 7. What's already been debugged (from the changelog)

This is the part worth knowing even without reading the full changelog: the student has been doing genuinely rigorous engineering work — not just "make it run," but repeated rounds of *"audit for bugs → fix → verify."* Highlights:

- Early versions could silently miscount how many readings were actually captured versus sent; this was found and replaced with an honest "coverage percentage" system.
- A subtle timing bug meant the receiver could stop responding correctly after just one successful recording (needed an explicit "are you ready?" handshake — fixed).
- The system used to just print CSI data live, which could overload the board at higher recording speeds; it was rebuilt to use a proper internal queue (a waiting line for data, so nothing gets dropped or garbled).
- As of the most recent entry, the student's own notes flag one still-open limitation: the system can prove a reading came from *the correct transmitter*, but not yet prove it came from *one specific sent packet* — i.e., correlation is close but not exact yet.

## 8. Roadmap (student's own stated order of remaining work)

1. Collect real, controlled `empty` / `metal` / `non_metal` recordings (this hasn't started).
2. Tighten packet-to-reading correlation.
3. Validate the full raw → cleaned → features pipeline as one trustworthy chain.
4. Keep training data and testing data strictly separate.
5. Train and evaluate an actual detector on controlled data only.
6. Move the trained detector onto the receiver board itself (so it doesn't need a PC).
7. Add the TFT screen for standalone results.
8. Optional: add SD card logging.

## 9. Open questions I'd flag for you (not assuming answers)

- No real `metal`/`non_metal` data has been collected yet — is that the next thing your student is working on, or is there a blocker?
- The docs mention COM port numbers shift on reconnect — worth confirming your student has a reliable way to identify which board is which each session.
- I have not yet opened the actual firmware code (`app_main.c`, `app_main.cpp`) or the Python scripts line by line — I only read the documentation and folder structure this session. Let me know if you want me to go deeper into any specific piece next.
