#!/usr/bin/env python3
"""Compare CSI captures taken under different conditions.

Answers one question: does the measured channel differ between conditions by
more than two supposedly-identical conditions differ from each other?

Three things about this script matter more than the statistics:

1. The unit of analysis is the RUN, not the frame. A 10-second capture holds
   ~1000 frames, but the WiFi channel's coherence time is long compared to
   10 ms, so those frames are strongly correlated. Treating them as independent
   samples produces absurd p-values on pure noise. Each run collapses to one
   profile; comparisons are between runs.

2. The per-subcarrier profile is the primary measurement, NOT the collapsed
   scalars that process_csi.py/extract_features.py produce. Those average
   amplitude across subcarriers, so a change that raises half the band and
   lowers the other half cancels exactly and reports nothing. That is the most
   likely way to manufacture a false negative here.

3. Amplitude is normalised per frame. CONFIG_GAIN_CONTROL is deliberately off
   on the ESP32-S3 (csi_capture.c note 1), so absolute magnitudes carry the
   receiver's uncompensated AGC. A separation that appears only in the raw
   profile means the gain changed, not the channel.

Pure standard library: tool/ has no numpy, scipy, pandas or matplotlib.

Usage:
    python compare_conditions.py --session <dir> [--baseline empty]
    python compare_conditions.py --condition empty=<dir> --condition metal=<dir>
    python compare_conditions.py --selftest
"""

import argparse
import csv
import itertools
import json
import math
import random
import re
import statistics
import sys
import tempfile
from array import array
from collections import Counter, defaultdict
from pathlib import Path

# Column indices. Positional on purpose: the 25-column header carries
# "sig_mode" at BOTH index 5 and index 21, so csv.DictReader silently keeps
# only the second and yields 24 keys. Never use DictReader on these files.
COL_TYPE, COL_ID, COL_MAC, COL_RSSI, COL_RATE = 0, 1, 2, 3, 4
COL_SIG_MODE, COL_MCS, COL_BANDWIDTH = 5, 6, 7
COL_NOISE_FLOOR = 14
COL_CHANNEL = 16
COL_LOCAL_TS = 18
COL_SIG_LEN = 20
COL_LEN, COL_FIRST_WORD, COL_DATA = 22, 23, 24

EXPECTED_HEADER = [
    "type", "id", "mac", "rssi", "rate", "sig_mode", "mcs", "bandwidth",
    "smoothing", "not_sounding", "aggregation", "stbc", "fec_coding", "sgi",
    "noise_floor", "ampdu_cnt", "channel", "secondary_channel",
    "local_timestamp", "ant", "sig_len", "sig_mode", "len", "first_word",
    "data",
]

# Bins that are structurally dead regardless of the data: DC of the LLTF block
# and DC of the HT-LTF block (256 values = 64 LLTF bins + 64 HT-LTF bins).
ALWAYS_DROP_BINS = (0, 64)
DEAD_BIN_FRACTION = 0.05

RUN_DIR_RE = re.compile(r"^b(\d+)_(.+)$", re.IGNORECASE)

CAVEAT = """\
THROWAWAY DIAGNOSTIC DATA. Not dataset material.

A large sheet occluding the line of sight changing the measured channel is
near-trivial physics. A positive here licenses exactly one claim: the
instrument responds to a large obstruction, so building the real rig is
worth it. The claim this project actually needs is metal vs sham, with
several objects per class, on a fixed rig."""


# --------------------------------------------------------------------------
# Loading
# --------------------------------------------------------------------------

def load_run(path, trim):
    """One capture file -> raw per-frame amplitudes plus confound diagnostics.

    Returns a dict, or raises ValueError if the file cannot be trusted.
    """
    with path.open(newline="", encoding="utf-8", errors="replace") as handle:
        rows = list(csv.reader(handle))
    if not rows:
        raise ValueError(f"{path.name}: empty file")
    if rows[0] != EXPECTED_HEADER:
        raise ValueError(
            f"{path.name}: unexpected header ({len(rows[0])} columns). "
            "Firmware and this tool disagree; refusing to guess."
        )

    body = [r for r in rows[1:] if r and len(r) == len(EXPECTED_HEADER)
            and r[COL_TYPE] == "CSI_DATA"]
    malformed = len(rows) - 1 - len(body)

    if len(body) > 2 * trim + 50:
        body = body[trim:len(body) - trim]

    lengths = Counter(int(r[COL_LEN]) for r in body if r[COL_LEN].lstrip("-").isdigit())
    if not lengths:
        raise ValueError(f"{path.name}: no usable rows")
    modal_len = lengths.most_common(1)[0][0]
    if modal_len % 4 != 0:
        raise ValueError(f"{path.name}: odd modal length {modal_len}")
    n_bins = modal_len // 2

    flat = array("f")
    skipped = Counter()
    rssi, noise, first_word_invalid = [], [], 0
    rate_mix, mcs_mix, sigmode_mix = Counter(), Counter(), Counter()

    for row in body:
        try:
            declared = int(row[COL_LEN])
        except ValueError:
            skipped["bad_len"] += 1
            continue
        if declared != modal_len:
            skipped["len_mismatch"] += 1
            continue
        try:
            values = json.loads(row[COL_DATA])
        except (ValueError, TypeError):
            skipped["bad_json"] += 1
            continue
        if len(values) != modal_len:
            skipped["ragged"] += 1
            continue

        # first_word_invalid corrupts only the first 32-bit word, i.e. bin 0's
        # I/Q pair. Bin 0 is dropped unconditionally, so these rows are kept
        # rather than discarded; the count is reported as a confound instead.
        if row[COL_FIRST_WORD] not in ("0", "false", "False"):
            first_word_invalid += 1

        for i in range(0, modal_len - 1, 2):
            flat.append(math.hypot(values[i], values[i + 1]))

        try:
            rssi.append(float(row[COL_RSSI]))
            noise.append(float(row[COL_NOISE_FLOOR]))
        except ValueError:
            pass
        rate_mix[row[COL_RATE]] += 1
        mcs_mix[row[COL_MCS]] += 1
        sigmode_mix[row[COL_SIG_MODE]] += 1

    frames = len(flat) // n_bins if n_bins else 0
    if frames < 20:
        raise ValueError(f"{path.name}: only {frames} usable frames")

    return {
        "path": path,
        "n_bins": n_bins,
        "frames": frames,
        "flat": flat,
        "modal_len": modal_len,
        "len_mix": lengths,
        "skipped": skipped,
        "malformed": malformed,
        "rssi_mean": statistics.fmean(rssi) if rssi else float("nan"),
        "noise_mean": statistics.fmean(noise) if noise else float("nan"),
        "first_word_invalid": first_word_invalid,
        "rate_mix": rate_mix,
        "mcs_mix": mcs_mix,
        "sigmode_mix": sigmode_mix,
    }


def discover(session_dir):
    """Find runs under a session directory, deriving condition and block.

    Expects collect_burst.py's layout: <session>/b<N>_<condition>/*.csv, and
    also picks up <session>/archive/b<N>_<condition>/*.csv, because any run
    classed 'partial' is moved there automatically.
    """
    found = defaultdict(list)
    for csv_path in sorted(session_dir.rglob("*.csv")):
        if ".filtered." in csv_path.name:
            continue
        match = RUN_DIR_RE.match(csv_path.parent.name)
        if not match:
            continue
        block, condition = int(match.group(1)), match.group(2).lower()
        found[condition].append((block, csv_path))
    return found


def grand_bin_means(runs, n_bins):
    """Mean amplitude per bin across every frame of every run."""
    totals = [0.0] * n_bins
    count = 0
    for run in runs:
        flat, frames = run["flat"], run["frames"]
        for f in range(frames):
            base = f * n_bins
            for b in range(n_bins):
                totals[b] += flat[base + b]
        count += frames
    return [t / count for t in totals] if count else totals


def choose_valid_bins(grand, n_bins):
    median = statistics.median(grand) or 1.0
    threshold = DEAD_BIN_FRACTION * median
    valid, dropped = [], []
    for b in range(n_bins):
        if b in ALWAYS_DROP_BINS or grand[b] < threshold:
            dropped.append(b)
        else:
            valid.append(b)
    return valid, dropped


def summarise(run, valid_bins):
    """Collapse one run to the per-run summary used for all comparisons."""
    n_bins, flat, frames = run["n_bins"], run["flat"], run["frames"]
    nb = len(valid_bins)
    raw = [0.0] * nb
    norm = [0.0] * nb
    frame_means, frame_spreads = [], []

    for f in range(frames):
        base = f * n_bins
        vals = [flat[base + b] for b in valid_bins]
        mean = sum(vals) / nb
        frame_means.append(mean)
        frame_spreads.append(statistics.pstdev(vals) if nb > 1 else 0.0)
        for i, v in enumerate(vals):
            raw[i] += v
            norm[i] += (v / mean) if mean > 0 else 0.0

    return {
        "raw_profile": [v / frames for v in raw],
        "norm_profile": [v / frames for v in norm],
        "rssi_mean": run["rssi_mean"],
        "amp_mean": statistics.fmean(frame_means),
        "amp_bin_spread": statistics.fmean(frame_spreads),
        "amp_temporal_std": statistics.pstdev(frame_means) if frames > 1 else 0.0,
        "frames": frames,
    }


# --------------------------------------------------------------------------
# Statistics
# --------------------------------------------------------------------------

def hedges_g(a, b):
    """Standardised mean difference, Hedges' small-sample correction."""
    na, nb = len(a), len(b)
    if na < 2 or nb < 2:
        return 0.0
    ma, mb = statistics.fmean(a), statistics.fmean(b)
    va, vb = statistics.variance(a), statistics.variance(b)
    pooled = ((na - 1) * va + (nb - 1) * vb) / (na + nb - 2)
    if pooled <= 0:
        return 0.0
    d = (mb - ma) / math.sqrt(pooled)
    correction = 1.0 - 3.0 / (4.0 * (na + nb) - 9.0)
    return d * correction


def _precompute(profiles):
    """Per-run (values, squares) so permutations are additions, not re-reads."""
    return [(p, [v * v for v in p]) for p in profiles]


def _group_stats(pre, indices, nbins):
    sums = [0.0] * nbins
    sqs = [0.0] * nbins
    for i in indices:
        vals, squares = pre[i]
        for b in range(nbins):
            sums[b] += vals[b]
            sqs[b] += squares[b]
    return sums, sqs


def _rms_g_from_moments(sa, qa, na, sb, qb, nb, nbins):
    """RMS Hedges' g across bins, from group sums and sums-of-squares."""
    if na < 2 or nb < 2:
        return 0.0, 0.0, []
    correction = 1.0 - 3.0 / (4.0 * (na + nb) - 9.0)
    gs = []
    for b in range(nbins):
        ma, mb_ = sa[b] / na, sb[b] / nb
        va = (qa[b] - na * ma * ma) / (na - 1)
        vb = (qb[b] - nb * mb_ * mb_) / (nb - 1)
        pooled = ((na - 1) * va + (nb - 1) * vb) / (na + nb - 2)
        gs.append(((mb_ - ma) / math.sqrt(pooled) * correction) if pooled > 0 else 0.0)
    rms = math.sqrt(sum(g * g for g in gs) / nbins)
    gmax = max(gs, key=abs) if gs else 0.0
    return rms, gmax, gs


def profile_separation(profiles_a, profiles_b):
    nbins = len(profiles_a[0])
    pre = _precompute(profiles_a + profiles_b)
    na, nb = len(profiles_a), len(profiles_b)
    sa, qa = _group_stats(pre, range(na), nbins)
    sb, qb = _group_stats(pre, range(na, na + nb), nbins)
    return _rms_g_from_moments(sa, qa, na, sb, qb, nb, nbins)


def permutation_test(profiles_a, profiles_b, n_perm, rng):
    """Exact when the split count allows it, Monte-Carlo otherwise."""
    nbins = len(profiles_a[0])
    na, nb = len(profiles_a), len(profiles_b)
    pool = profiles_a + profiles_b
    total_n = na + nb
    pre = _precompute(pool)

    grand_s, grand_q = _group_stats(pre, range(total_n), nbins)
    observed, obs_max, obs_gs = profile_separation(profiles_a, profiles_b)

    exact = math.comb(total_n, na) <= n_perm
    if exact:
        splits = itertools.combinations(range(total_n), na)
    else:
        splits = (rng.sample(range(total_n), na) for _ in range(n_perm))

    hits = trials = 0
    null_max = []
    for idx in splits:
        sa, qa = _group_stats(pre, idx, nbins)
        sb = [grand_s[b] - sa[b] for b in range(nbins)]
        qb = [grand_q[b] - qa[b] for b in range(nbins)]
        stat, gmax, _ = _rms_g_from_moments(sa, qa, na, sb, qb, nb, nbins)
        if stat >= observed:
            hits += 1
        trials += 1
        null_max.append(abs(gmax))

    null_max.sort()
    return {
        "observed": observed,
        "max_g": obs_max,
        "per_bin_g": obs_gs,
        "p_value": (1 + hits) / (1 + trials),
        "mode": "exact" if exact else "monte_carlo",
        "trials": trials,
        "null_max_sorted": null_max,
    }


def noise_floor(profiles, cap=5000):
    """How far apart are two halves of the SAME condition? The control."""
    n = len(profiles)
    half = n // 2
    if n < 4:
        return None
    values = []
    for idx in itertools.islice(itertools.combinations(range(n), half), cap):
        if 0 not in idx:  # count each complementary split once
            continue
        a = [profiles[i] for i in idx]
        b = [profiles[i] for i in range(n) if i not in idx]
        values.append(profile_separation(a, b)[0])
    if not values:
        return None
    values.sort()
    return {
        "median": values[len(values) // 2],
        "q95": values[min(int(0.95 * len(values)), len(values) - 1)],
        "max": values[-1],
        "n_splits": len(values),
    }


def scalar_comparison(runs_a, runs_b, key, rng, n_perm=20000):
    a = [r[key] for r in runs_a]
    b = [r[key] for r in runs_b]
    if any(math.isnan(v) for v in a + b):
        return None
    observed = hedges_g(a, b)
    pool = a + b
    na, total = len(a), len(a) + len(b)
    exact = math.comb(total, na) <= n_perm
    splits = (itertools.combinations(range(total), na) if exact
              else (rng.sample(range(total), na) for _ in range(n_perm)))
    hits = trials = 0
    for idx in splits:
        s = set(idx)
        ga = [pool[i] for i in range(total) if i in s]
        gb = [pool[i] for i in range(total) if i not in s]
        if abs(hedges_g(ga, gb)) >= abs(observed):
            hits += 1
        trials += 1
    return {
        "g": observed,
        "p_value": (1 + hits) / (1 + trials),
        "mean_a": statistics.fmean(a),
        "mean_b": statistics.fmean(b),
    }


def block_consistency(by_block_a, by_block_b, direction):
    """Do individual blocks agree in sign? Descriptive, not an extra test.

    `direction` is derived from the same data, so this answers 'is the effect
    steady over the session', not 'is it significant'.
    """
    blocks = sorted(set(by_block_a) & set(by_block_b))
    if not blocks:
        return None

    def project(profile):
        return sum(d * v for d, v in zip(direction, profile))

    wins = sum(1 for k in blocks if project(by_block_b[k]) > project(by_block_a[k]))
    n = len(blocks)
    tail = sum(math.comb(n, i) for i in range(wins, n + 1))
    p = min(1.0, 2.0 * tail / (2 ** n))
    return {"blocks": n, "agree": wins, "p_value": p}


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

def fmt(value, places=3):
    if value is None or (isinstance(value, float) and math.isnan(value)):
        return "n/a"
    return f"{value:.{places}f}"


def confound_table(summaries, raws, conditions):
    """Metrics that must NOT differ by condition. If they do, the 'effect'
    may be a link-adaptation artefact rather than a channel measurement."""
    print("\n2. CONFOUND TABLE  (these must NOT differ by condition)")
    header = f"   {'metric':<22}" + "".join(f"{c:<20}" for c in conditions)
    print(header)

    def spread(values):
        if not values:
            return "n/a"
        if len(values) == 1:
            return f"{values[0]:.1f}"
        return f"{statistics.fmean(values):.1f} +- {statistics.pstdev(values):.1f}"

    rows = {
        "frames/run": lambda c: spread([s["frames"] for s in summaries[c]]),
        "noise_floor mean": lambda c: spread([s["noise_mean"] for s in raws[c]
                                              if not math.isnan(s["noise_mean"])]),
        "first_word invalid": lambda c: spread([
            100.0 * s["first_word_invalid"] / max(s["frames"], 1) for s in raws[c]]) + "%",
    }
    for label, fn in rows.items():
        print(f"   {label:<22}" + "".join(f"{fn(c):<20}" for c in conditions))

    flags = []
    for label, key in (("modal len", "modal_len"),):
        cells = []
        for c in conditions:
            vals = Counter(s[key] for s in raws[c])
            cells.append(",".join(f"{k}" for k in sorted(vals)))
        print(f"   {label:<22}" + "".join(f"{v:<20}" for v in cells))
        if len(set(cells)) > 1:
            flags.append(label)

    for label, key in (("rate mix", "rate_mix"), ("mcs mix", "mcs_mix"),
                       ("sig_mode mix", "sigmode_mix")):
        cells = []
        for c in conditions:
            merged = Counter()
            for s in raws[c]:
                merged.update(s[key])
            top = merged.most_common(1)
            total = sum(merged.values()) or 1
            cells.append(f"{top[0][0]}:{100*top[0][1]//total}%" if top else "n/a")
        print(f"   {label:<22}" + "".join(f"{v:<20}" for v in cells))
        if len(set(c.split(":")[0] for c in cells)) > 1:
            flags.append(label)

    frame_counts = [statistics.fmean([s["frames"] for s in summaries[c]]) for c in conditions]
    if frame_counts and min(frame_counts) < 0.8 * max(frame_counts):
        flags.append("frames/run (>20% difference)")

    if flags:
        print(f"   VERDICT: FLAGGED -> {', '.join(flags)}")
    else:
        print("   VERDICT: no confound flagged.")
    return flags


def decide(result, floor, flags, consistency, sham_ratio):
    """The pre-registered decision rule. Applied without discretion."""
    reasons, verdict = [], None
    p = result["p_value"]
    t = result["observed"]

    detectable = (
        p <= 0.01
        and floor is not None and t > floor["max"]
        and not flags
        and consistency is not None
        and consistency["agree"] >= math.ceil(0.875 * consistency["blocks"])
    )
    inconclusive = (
        (p <= 0.05 and floor is not None and t <= floor["max"])
        or (floor is not None and floor["q95"] > 1.0)
        or bool(flags)
        or (sham_ratio is not None and sham_ratio >= 0.67)
    )

    if detectable:
        verdict = "DETECTABLE"
        reasons.append(f"p = {p:.2g} <= 0.01")
        reasons.append(f"statistic {t:.3f} exceeds every empty-vs-empty split "
                       f"(max {floor['max']:.3f})")
        reasons.append("no confound flagged")
        reasons.append(f"{consistency['agree']}/{consistency['blocks']} blocks agree")
    elif inconclusive:
        verdict = "INCONCLUSIVE"
        if p <= 0.05 and floor is not None and t <= floor["max"]:
            reasons.append("significant but inside the noise range "
                           "(overfit-to-noise signature)")
        if floor is not None and floor["q95"] > 1.0:
            reasons.append(f"noise floor itself is large (q95 {floor['q95']:.3f} > 1.0); "
                           "the bench is not stable enough to answer")
        if flags:
            reasons.append(f"confound flagged: {', '.join(flags)}")
        if sham_ratio is not None and sham_ratio >= 0.67:
            reasons.append(f"sham separates {sham_ratio:.0%} as strongly as metal - "
                           "this measured 'an object', not 'metal'")
    else:
        verdict = "NOT DETECTABLE"
        reasons.append(f"p = {p:.2g} > 0.05")
        if floor is not None:
            reasons.append(f"statistic {t:.3f} within the noise floor "
                           f"(q95 {floor['q95']:.3f})")
            reasons.append(f"SENSITIVITY BOUND: this design would have caught an "
                           f"effect of RMS g ~ {floor['q95']:.2f}; nothing larger is present")
    return verdict, reasons


# --------------------------------------------------------------------------
# Main analysis
# --------------------------------------------------------------------------

def analyse(conditions_map, baseline, trim, n_perm, seed, json_path):
    rng = random.Random(seed)

    raws, blocks_of = {}, {}
    print("=== compare_conditions.py ===\n")
    print("1. RUN INVENTORY")
    print(f"   {'cond':<8}{'block':<7}{'frames':<9}{'skipped':<12}file")

    for condition in sorted(conditions_map):
        loaded, block_ids = [], []
        for block, path in sorted(conditions_map[condition]):
            try:
                run = load_run(path, trim)
            except ValueError as exc:
                print(f"   DISCARDED: {exc}")
                continue
            loaded.append(run)
            block_ids.append(block)
            skipped = sum(run["skipped"].values())
            print(f"   {condition:<8}{block:<7}{run['frames']:<9}{skipped:<12}"
                  f"{path.parent.name}/{path.name[:40]}")
        if loaded:
            raws[condition] = loaded
            blocks_of[condition] = block_ids

    if len(raws) < 2:
        print("\nNeed at least two conditions with usable runs.")
        return 2

    n_bins = raws[next(iter(raws))][0]["n_bins"]
    for condition, runs in raws.items():
        for run in runs:
            if run["n_bins"] != n_bins:
                print(f"\nBin-count mismatch: {run['path'].name} has {run['n_bins']}, "
                      f"expected {n_bins}. Cannot compare.")
                return 2

    all_runs = [r for runs in raws.values() for r in runs]
    grand = grand_bin_means(all_runs, n_bins)
    valid_bins, dropped = choose_valid_bins(grand, n_bins)

    summaries = {c: [summarise(r, valid_bins) for r in runs] for c, runs in raws.items()}
    conditions = sorted(summaries)

    flags = confound_table(summaries, raws, conditions)

    print(f"\n3. VALID BINS\n   {n_bins} bins present, {len(valid_bins)} kept, "
          f"{len(dropped)} dropped (DC + near-zero):\n   {dropped}")

    if baseline not in summaries:
        baseline = conditions[0]
    base_norm = [s["norm_profile"] for s in summaries[baseline]]
    floor = noise_floor(base_norm)
    floor_raw = noise_floor([s["raw_profile"] for s in summaries[baseline]])

    print(f"\n4. NOISE FLOOR  ({baseline} vs {baseline}, "
          f"{floor['n_splits'] if floor else 0} exhaustive half-splits)")
    if floor:
        print(f"   {'statistic':<24}{'median':<10}{'q95':<10}{'max':<10}")
        print(f"   {'norm profile RMS-g':<24}{fmt(floor['median']):<10}"
              f"{fmt(floor['q95']):<10}{fmt(floor['max']):<10}")
        if floor_raw:
            print(f"   {'raw  profile RMS-g':<24}{fmt(floor_raw['median']):<10}"
                  f"{fmt(floor_raw['q95']):<10}{fmt(floor_raw['max']):<10}")
    else:
        print(f"   Not computable: need >= 4 {baseline} runs, have "
              f"{len(base_norm)}. Without this there is no control.")

    print(f"\n5. CONDITION COMPARISONS  (run-level, exact permutation where possible)")
    results, dumped = {}, {}
    for condition in conditions:
        if condition == baseline:
            continue
        a_norm = [s["norm_profile"] for s in summaries[baseline]]
        b_norm = [s["norm_profile"] for s in summaries[condition]]
        res = permutation_test(a_norm, b_norm, n_perm, rng)
        results[condition] = res

        a_raw = [s["raw_profile"] for s in summaries[baseline]]
        b_raw = [s["raw_profile"] for s in summaries[condition]]
        res_raw = permutation_test(a_raw, b_raw, n_perm, rng)

        ratio = (res["observed"] / floor["max"]) if floor and floor["max"] > 0 else float("nan")
        marker = "EXCEEDS FLOOR" if floor and res["observed"] > floor["max"] else "within floor"
        print(f"\n   {condition} vs {baseline}   [{res['mode']}, {res['trials']} splits]")
        print(f"     norm profile RMS-g   {fmt(res['observed'])}   "
              f"p = {res['p_value']:.2g}   ratio {fmt(ratio, 1)}x   {marker}")
        fwe = sum(1 for m in res["null_max_sorted"] if m >= abs(res["max_g"]))
        print(f"     max |g| per bin      {fmt(abs(res['max_g']))}   "
              f"FWE p = {(1 + fwe) / (1 + len(res['null_max_sorted'])):.2g}")
        print(f"     raw  profile RMS-g   {fmt(res_raw['observed'])}   "
              f"p = {res_raw['p_value']:.2g}")

        scalars = {}
        for key, label in (("rssi_mean", "rssi_mean"), ("amp_mean", "amp_mean"),
                           ("amp_bin_spread", "amp_bin_spread"),
                           ("amp_temporal_std", "amp_temporal_std")):
            sc = scalar_comparison(summaries[baseline], summaries[condition], key, rng)
            if sc:
                scalars[key] = sc
                delta = sc["mean_b"] - sc["mean_a"]
                print(f"     {label:<20} g = {sc['g']:+.2f}  "
                      f"(delta {delta:+.2f})   p = {sc['p_value']:.2g}")

        direction = [b - a for a, b in zip(
            [statistics.fmean(col) for col in zip(*a_norm)],
            [statistics.fmean(col) for col in zip(*b_norm)])]
        by_a = {blk: s["norm_profile"] for blk, s in zip(blocks_of[baseline], summaries[baseline])}
        by_b = {blk: s["norm_profile"] for blk, s in zip(blocks_of[condition], summaries[condition])}
        cons = block_consistency(by_a, by_b, direction)
        if cons:
            print(f"     block consistency    {cons['agree']}/{cons['blocks']} "
                  f"same direction, sign-test p = {cons['p_value']:.2g}  (descriptive)")
        dumped[condition] = {"norm": res, "raw": res_raw,
                             "scalars": scalars, "blocks": cons}

    primary = "metal" if "metal" in results else next(iter(results))
    res = results[primary]
    sham_ratio = None
    if "sham" in results and res["observed"] > 0:
        sham_ratio = results["sham"]["observed"] / res["observed"]

    a_norm = [s["norm_profile"] for s in summaries[baseline]]
    b_norm = [s["norm_profile"] for s in summaries[primary]]
    direction = [b - a for a, b in zip(
        [statistics.fmean(col) for col in zip(*a_norm)],
        [statistics.fmean(col) for col in zip(*b_norm)])]
    cons = block_consistency(
        {blk: s["norm_profile"] for blk, s in zip(blocks_of[baseline], summaries[baseline])},
        {blk: s["norm_profile"] for blk, s in zip(blocks_of[primary], summaries[primary])},
        direction)

    order = sorted(range(len(valid_bins)), key=lambda i: -abs(res["per_bin_g"][i]))[:10]
    print(f"\n6. PER-BIN DETAIL ({primary} vs {baseline}, top 10 by |g|, norm profile)")
    print(f"   {'bin':<7}{baseline+'_mean':<16}{primary+'_mean':<16}g")
    mean_a = [statistics.fmean(col) for col in zip(*a_norm)]
    mean_b = [statistics.fmean(col) for col in zip(*b_norm)]
    for i in order:
        print(f"   {valid_bins[i]:<7}{fmt(mean_a[i]):<16}{fmt(mean_b[i]):<16}"
              f"{res['per_bin_g'][i]:+.2f}")

    verdict, reasons = decide(res, floor, flags, cons, sham_ratio)
    print(f"\n7. DECISION  (rule fixed before capture)\n\n   >>> {verdict} <<<\n")
    for reason in reasons:
        print(f"   - {reason}")
    if sham_ratio is not None:
        print(f"\n   sham separates at {sham_ratio:.0%} of {primary}'s statistic.")
        print("   A positive only becomes a claim about METAL when metal-vs-sham "
              "clears the floor.")
    print()
    for line in CAVEAT.splitlines():
        print(f"   {line}")
    print()

    if json_path:
        Path(json_path).write_text(json.dumps({
            "verdict": verdict, "reasons": reasons, "baseline": baseline,
            "conditions": {c: len(summaries[c]) for c in conditions},
            "valid_bins": valid_bins, "dropped_bins": dropped,
            "noise_floor": floor, "flags": flags,
            "results": {c: {"norm_rms_g": d["norm"]["observed"],
                            "norm_p": d["norm"]["p_value"],
                            "raw_rms_g": d["raw"]["observed"],
                            "raw_p": d["raw"]["p_value"],
                            "scalars": d["scalars"], "blocks": d["blocks"]}
                        for c, d in dumped.items()},
        }, indent=2) + "\n", encoding="utf-8")
        print(f"   machine-readable results -> {json_path}\n")

    return 0 if verdict != "INCONCLUSIVE" else 1


# --------------------------------------------------------------------------
# Self-test
# --------------------------------------------------------------------------

def _synth_capture(path, profile, frames, rng, gain=1.0, jitter=0.06):
    """Write a CSV in the real 25-column format with a known bin profile."""
    path.parent.mkdir(parents=True, exist_ok=True)
    n_bins = len(profile)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(EXPECTED_HEADER)
        for frame in range(frames):
            scale = gain * (1.0 + rng.gauss(0, jitter))
            values = []
            for amp in profile:
                a = max(0.0, amp * scale * (1.0 + rng.gauss(0, jitter)))
                phase = rng.uniform(0, 2 * math.pi)
                values.append(int(round(a * math.cos(phase))))
                values.append(int(round(a * math.sin(phase))))
            writer.writerow([
                "CSI_DATA", frame, "aa:bb:cc:dd:ee:ff", -40, "11", "1", "7", "0",
                "0", "1", "1", "0", "0", "0", "-96", "0", "11", "0",
                frame * 10000, "0", "86", "1", n_bins * 2, "0",
                json.dumps(values),
            ])


def _base_profile(n_bins, rng):
    return [30.0 + 12.0 * math.sin(b / 7.0) + rng.gauss(0, 0.6) for b in range(n_bins)]


def _run_case(name, root, make_profile, n_runs=8, frames=120, seed=5):
    rng = random.Random(seed)
    base = _base_profile(64, random.Random(99))
    conditions = {}
    for condition in ("empty", "probe"):
        entries = []
        for block in range(1, n_runs + 1):
            profile, gain = make_profile(condition, base, rng)
            path = root / name / f"b{block}_{condition}" / "run.csv"
            _synth_capture(path, profile, frames, rng, gain=gain)
            entries.append((block, path))
        conditions[condition] = entries
    return conditions


def selftest():
    print("compare_conditions.py --selftest\n")
    failures = []
    root = Path(tempfile.mkdtemp(prefix="cmpcond_"))

    def quiet(conditions_map, baseline="empty"):
        import io
        import contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            analyse(conditions_map, baseline, trim=0, n_perm=20000, seed=1, json_path=None)
        return buf.getvalue()

    # Case 1: null -- identical distributions. Must NOT be detectable.
    # Catches frame-level pseudo-replication: if runs were pooled as frames,
    # this reports a huge effect.
    def null_profile(condition, base, rng):
        return [v + rng.gauss(0, 0.8) for v in base], 1.0
    out = quiet(_run_case("null", root, null_profile))
    ok = "NOT DETECTABLE" in out
    print(f"  [{'PASS' if ok else 'FAIL'}] null case -> not detectable")
    if not ok:
        failures.append("null case reported an effect; frames may be treated as samples")

    # Case 2: shape change with ZERO mean change. The normalised profile must
    # catch it; amp_mean must miss it. This is the false-negative guard.
    def shape_profile(condition, base, rng):
        if condition == "empty":
            return [v + rng.gauss(0, 0.8) for v in base], 1.0
        shifted = [v * (1.30 if b < len(base) // 2 else 0.70)
                   for b, v in enumerate(base)]
        scale = sum(base) / sum(shifted)
        return [v * scale + rng.gauss(0, 0.8) for v in shifted], 1.0
    out = quiet(_run_case("shape", root, shape_profile))
    caught = "DETECTABLE" in out and "NOT DETECTABLE" not in out
    amp_line = [l for l in out.splitlines() if "amp_mean" in l]
    amp_missed = bool(amp_line) and abs(float(amp_line[0].split("g =")[1].split()[0])) < 2.0
    print(f"  [{'PASS' if caught else 'FAIL'}] shape case -> detected by profile")
    print(f"  [{'PASS' if amp_missed else 'FAIL'}] shape case -> missed by amp_mean "
          f"(proves the profile is doing the work)")
    if not caught:
        failures.append("shape change with zero mean change was not detected")
    if not amp_missed:
        failures.append("amp_mean unexpectedly caught the shape change")

    # Case 3: pure gain. Raw must separate; normalised must not.
    def gain_profile(condition, base, rng):
        noisy = [v + rng.gauss(0, 0.8) for v in base]
        return (noisy, 1.0) if condition == "empty" else (noisy, 1.45)
    out = quiet(_run_case("gain", root, gain_profile))
    comp = [l for l in out.splitlines() if "norm profile RMS-g" in l and "p =" in l]
    raw_line = [l for l in out.splitlines() if "raw  profile RMS-g" in l and "p =" in l]
    norm_stat = float(comp[0].split()[3]) if comp else 99.0
    raw_stat = float(raw_line[0].split()[3]) if raw_line else 0.0
    # The real guarantee is not "raw > norm" but that the normalised statistic
    # stays INSIDE the noise floor, i.e. a pure AGC change is reported as
    # nothing at all. raw and amp_mean will both scream on this input.
    within = "within floor" in comp[0] if comp else False
    gain_ok = raw_stat > norm_stat and within
    print(f"  [{'PASS' if gain_ok else 'FAIL'}] gain case -> normalised stays inside "
          f"the noise floor ({norm_stat:.2f}, {'within' if within else 'EXCEEDS'}) "
          f"while raw separates ({raw_stat:.2f})")
    if not gain_ok:
        failures.append("gain invariance failed: a pure AGC change is not being "
                        "absorbed by per-frame normalisation")

    import shutil
    shutil.rmtree(root, ignore_errors=True)

    print()
    if failures:
        print("SELF-TEST FAILED")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("SELF-TEST PASSED\n")
    print("  The script can detect a shape-only change, ignores a pure gain")
    print("  change, and reports nothing on identical distributions.")
    return 0


# --------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Compare CSI captures across conditions.",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--session", help="Directory holding b<N>_<condition>/ run folders")
    parser.add_argument("--condition", action="append", default=[],
                        metavar="NAME=DIR", help="Explicit condition directory; repeatable")
    parser.add_argument("--baseline", default="empty",
                        help="Condition defining the noise floor (default: empty)")
    parser.add_argument("--trim-frames", type=int, default=25,
                        help="Drop this many frames from each end of every run. "
                             "Runs are ~400 frames (20s at 20Hz), so 25 removes "
                             "start/end transients without spending much data.")
    parser.add_argument("--permutations", type=int, default=20000,
                        help="Monte-Carlo permutations when exact is infeasible")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--json", dest="json_path", help="Write machine-readable results here")
    parser.add_argument("--selftest", action="store_true",
                        help="Validate the script against synthetic data and exit")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    conditions_map = defaultdict(list)
    if args.session:
        found = discover(Path(args.session))
        if not found:
            print(f"No b<N>_<condition>/*.csv runs found under {args.session}")
            return 2
        for condition, entries in found.items():
            conditions_map[condition].extend(entries)
    for spec in args.condition:
        if "=" not in spec:
            print(f"--condition needs NAME=DIR, got {spec!r}")
            return 2
        name, _, directory = spec.partition("=")
        for block, path in enumerate(sorted(Path(directory).rglob("*.csv")), start=1):
            if ".filtered." not in path.name:
                conditions_map[name.lower()].append((block, path))

    if not conditions_map:
        parser.print_help()
        return 2

    return analyse(conditions_map, args.baseline.lower(), args.trim_frames,
                   args.permutations, args.seed, args.json_path)


if __name__ == "__main__":
    sys.exit(main())
