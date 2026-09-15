#!/usr/bin/env python3
"""Drive a randomised-block capture session, one prompt per run.

Twenty-four captures in a randomised order, typed by hand, is where a blocked
design quietly stops being one: a condition run in the wrong block, a run
silently failing its quality gate, a repeat spliced into the wrong place. This
walks the pre-registered order, checks every run against the abort rules the
moment it finishes, and refuses to move on until the run is good.

It is a driver only. It decides nothing the session notes have not already
fixed in advance, and it never touches the analysis.

    python run_probe_session.py --session diagnostics/metal_probe_2026-09-15

Safe to stop and restart: completed runs are detected and skipped.
"""

import argparse
import json
import random
import subprocess
import sys
from pathlib import Path

CONDITIONS = ["empty", "metal", "sham"]

# Set in the session notes before any data existed. Do not change.
DEFAULT_SEED = 11
DEFAULT_BLOCKS = 8

# A run must clear all four to count. These mirror the notes file exactly.
MIN_COVERAGE = 0.9

DESCRIPTIONS = {
    "empty": "NOTHING on the stand (leave the stand itself in place)",
    "metal": "the STACKED BRAKE DISCS on the stand",
    "sham": "the CARDBOARD DISC on the stand",
}


def block_order(blocks, seed):
    rng = random.Random(seed)
    order = []
    for block in range(1, blocks + 1):
        conditions = list(CONDITIONS)
        rng.shuffle(conditions)
        order.append((block, conditions))
    return order


def metadata_for(session, run_id):
    """Find a finished run's metadata, wherever collect_burst put it."""
    for folder in (session / run_id, session / "archive" / run_id):
        if not folder.is_dir():
            continue
        for path in folder.glob("*.json"):
            if "features" in path.name:
                continue
            try:
                return json.loads(path.read_text(encoding="utf-8"))
            except (ValueError, OSError):
                return None
    return None


def check(meta):
    """Apply the pre-registered abort rules. Returns a list of failures."""
    if meta is None:
        return ["no metadata written"]
    problems = []
    if meta.get("capture_status") != "success":
        problems.append(f"capture_status={meta.get('capture_status')}")
    if meta.get("receiver_queue_drops", 0) != 0:
        problems.append(f"receiver_queue_drops={meta.get('receiver_queue_drops')}")
    if meta.get("transmitter_failures", 0) != 0:
        problems.append(f"transmitter_failures={meta.get('transmitter_failures')}")
    coverage = meta.get("sample_coverage_ratio")
    if coverage is None or coverage < MIN_COVERAGE:
        problems.append(f"sample_coverage_ratio={coverage}")
    return problems


def capture(args, block, condition):
    run_id = f"b{block}_{condition}"
    command = [
        sys.executable, str(Path(__file__).with_name("collect_burst.py")),
        "--transmitter", args.transmitter,
        "--receiver", args.receiver,
        "--receiver-baud", args.receiver_baud,
        "--label", "other",
        "--object-name", f"DIAG_{condition}",
        "--duration-ms", str(args.duration_ms),
        "--rate-hz", str(args.rate_hz),
        "--output-dir", str(args.session),
        "--run-id", run_id,
    ]
    subprocess.run(command, check=False)
    return run_id, check(metadata_for(Path(args.session), run_id))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--session", required=True)
    parser.add_argument("--transmitter", default="COM5")
    parser.add_argument("--receiver", default="COM9")
    parser.add_argument("--receiver-baud", default="921600")
    parser.add_argument("--duration-ms", type=int, default=20000)
    parser.add_argument("--rate-hz", type=int, default=20)
    parser.add_argument("--blocks", type=int, default=DEFAULT_BLOCKS)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    args = parser.parse_args()

    session = Path(args.session)
    if not session.is_dir():
        print(f"Session directory does not exist: {session}")
        return 2
    if not (session / "DO_NOT_USE_AS_DATASET.txt").exists():
        print(f"Refusing to run: {session} has no DO_NOT_USE_AS_DATASET.txt.")
        print("That file carries the pre-registered rules this session follows.")
        return 2

    order = block_order(args.blocks, args.seed)
    total = sum(len(c) for _, c in order)

    print("=" * 74)
    print(f"PROBE SESSION  -  {total} runs, {args.duration_ms // 1000}s at "
          f"{args.rate_hz} Hz, seed {args.seed}")
    print("=" * 74)
    for block, conditions in order:
        print(f"  block {block}: " + "  ->  ".join(conditions))
    print()
    print("Everything is STATIONARY. Place the object, hands off, sit still.")
    print("After you press enter there is ~5s before capture starts.")
    print("If a board or cable moves, STOP - the session restarts from block 1.")
    print()

    done, repeats = 0, []
    for block, conditions in order:
        print("-" * 74)
        print(f"BLOCK {block} of {args.blocks}")
        print("-" * 74)
        for condition in conditions:
            run_id = f"b{block}_{condition}"
            existing = metadata_for(session, run_id)
            if existing is not None and not check(existing):
                done += 1
                print(f"  [{done}/{total}] {run_id} already captured and clean - skipping")
                continue

            while True:
                print()
                print(f"  [{done + 1}/{total}]  BLOCK {block}  ->  {condition.upper()}")
                print(f"           Place {DESCRIPTIONS[condition]}.")
                reply = input("           Enter to capture, 's' to skip, 'q' to quit: ").strip().lower()
                if reply == "q":
                    print("\nStopped. Re-run this command to pick up where you left off.")
                    return 1
                if reply == "s":
                    print("           skipped")
                    break

                run_id, problems = capture(args, block, condition)
                if not problems:
                    done += 1
                    print(f"           OK - run is clean")
                    break
                print(f"           BAD RUN: {', '.join(problems)}")
                print(f"           Per the abort rules this must be repeated.")
                again = input("           Enter to retry now, 'l' to repeat at end of block: ").strip().lower()
                if again == "l":
                    repeats.append((block, condition))
                    break

        while repeats:
            block_r, condition_r = repeats.pop(0)
            print(f"\n  REPEAT from block {block_r}: {condition_r.upper()}")
            input(f"           Place {DESCRIPTIONS[condition_r]}, then Enter: ")
            _, problems = capture(args, block_r, condition_r)
            if problems:
                print(f"           STILL BAD: {', '.join(problems)}")
                repeats.append((block_r, condition_r))
                if input("           Enter to try again, 'q' to stop: ").strip().lower() == "q":
                    return 1
            else:
                done += 1
                print("           OK")

    print()
    print("=" * 74)
    print(f"SESSION COMPLETE  -  {done}/{total} clean runs")
    print("=" * 74)
    print("\nPhotograph the rig again and fill in the end time in")
    print(f"  {session / 'DO_NOT_USE_AS_DATASET.txt'}")
    print("\nThen analyse:")
    print(f"  python wi-sense/tool/compare_conditions.py --session {session} \\")
    print(f"      --baseline empty --json {session / 'result.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
