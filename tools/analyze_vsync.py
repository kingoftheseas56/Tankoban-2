#!/usr/bin/env python3
"""Phase 0 vsync timing analyzer.

Reads the CSV produced by FrameCanvas's F12 logger and prints a verdict on
whether Qt RHI's render-interval timing is suitable as the input signal for
display-resample (MPV-style judder elimination).

Usage:
    python tools/analyze_vsync.py _vsync_timing.csv

Decision criteria (from the plan):
  - Qt mean within 0.1% of DXGI mean, stddev < 1ms  -> proceed (clean)
  - Qt mean matches DXGI but stddev 1-5ms          -> proceed (filter needed)
  - Qt diverges from DXGI by > 0.1%                -> need direct DXGI path
  - Either source unusable                          -> hardware/driver issue
"""
from __future__ import annotations

import csv
import math
import sys
from pathlib import Path
from statistics import mean, stdev


def percentile(sorted_values, pct):
    if not sorted_values:
        return 0.0
    k = (len(sorted_values) - 1) * (pct / 100.0)
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return sorted_values[int(k)]
    return sorted_values[f] + (sorted_values[c] - sorted_values[f]) * (k - f)


def fmt_ms(ns: float) -> str:
    return f"{ns / 1e6:7.3f} ms"


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        sys.exit(1)

    path = Path(argv[1])
    if not path.exists():
        print(f"ERROR: file not found: {path}")
        sys.exit(2)

    qt_intervals_ns: list[int] = []
    dxgi_present_refresh: list[int] = []
    dxgi_sync_qpc: list[int] = []
    dxgi_sync_refresh: list[int] = []
    dxgi_valid_count = 0

    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            qt_iv = int(row["qt_interval_ns"])
            if qt_iv > 0:  # skip first sample where prev is unset
                qt_intervals_ns.append(qt_iv)
            if int(row["dxgi_valid"]) == 1:
                dxgi_valid_count += 1
                dxgi_present_refresh.append(int(row["present_refresh"]))
                dxgi_sync_qpc.append(int(row["sync_qpc_time"]))
                dxgi_sync_refresh.append(int(row["sync_refresh"]))

    n = len(qt_intervals_ns)
    if n < 30:
        print(f"WARNING: only {n} samples; need ~60 seconds of playback (3000+ samples)")

    # ── Qt-observed interval stats ─────────────────────────────────────────
    qt_intervals_ns_sorted = sorted(qt_intervals_ns)
    qt_mean_ns = mean(qt_intervals_ns) if n else 0.0
    qt_stddev_ns = stdev(qt_intervals_ns) if n > 1 else 0.0
    qt_min_ns = min(qt_intervals_ns) if n else 0
    qt_max_ns = max(qt_intervals_ns) if n else 0
    qt_p50_ns = percentile(qt_intervals_ns_sorted, 50)
    qt_p99_ns = percentile(qt_intervals_ns_sorted, 99)
    qt_fps = 1e9 / qt_mean_ns if qt_mean_ns > 0 else 0.0

    # Outlier count (>2x mean = a missed/skipped vsync)
    outliers = sum(1 for iv in qt_intervals_ns if iv > 2 * qt_mean_ns)

    print("=" * 60)
    print("VSYNC TIMING REPORT")
    print("=" * 60)
    print(f"Samples:          {n} total intervals (DXGI valid: {dxgi_valid_count})")
    print()
    print("Qt-observed render() interval:")
    print(f"  mean:    {fmt_ms(qt_mean_ns)}    (~{qt_fps:.3f} Hz)")
    print(f"  stddev:  {fmt_ms(qt_stddev_ns)}")
    print(f"  min:     {fmt_ms(qt_min_ns)}")
    print(f"  max:     {fmt_ms(qt_max_ns)}")
    print(f"  p50:     {fmt_ms(qt_p50_ns)}")
    print(f"  p99:     {fmt_ms(qt_p99_ns)}")
    print(f"  outliers (>2x mean): {outliers}  ({100.0 * outliers / n:.2f}%)")
    print()

    # ── DXGI cross-check ──────────────────────────────────────────────────
    dxgi_mean_ns = 0.0
    dxgi_diff_pct = 0.0
    have_dxgi = dxgi_valid_count >= 30 and len(dxgi_sync_qpc) > 1

    if have_dxgi:
        # Derive DXGI vsync interval from consecutive SyncQPCTime values where
        # SyncRefreshCount actually advanced. QPC is in 100ns ticks on Windows.
        intervals_qpc = []
        for i in range(1, len(dxgi_sync_qpc)):
            d_qpc = dxgi_sync_qpc[i] - dxgi_sync_qpc[i - 1]
            d_refresh = dxgi_sync_refresh[i] - dxgi_sync_refresh[i - 1]
            if d_refresh > 0 and d_qpc > 0:
                # Average QPC ticks per refresh in this delta. QPC frequency
                # is typically the system perf counter; assume it's 10MHz
                # (100ns granularity) on Windows for QueryPerformanceCounter.
                # If actual frequency differs, the DXGI mean reported below
                # may need calibration — for now the relative comparison
                # is what matters.
                intervals_qpc.append(d_qpc / d_refresh)

        if intervals_qpc:
            # Convert QPC ticks (assumed 10MHz / 100ns) to ns
            dxgi_mean_qpc = mean(intervals_qpc)
            dxgi_mean_ns = dxgi_mean_qpc * 100.0
            dxgi_fps = 1e9 / dxgi_mean_ns if dxgi_mean_ns > 0 else 0.0
            dxgi_diff_pct = (
                100.0 * (qt_mean_ns - dxgi_mean_ns) / dxgi_mean_ns if dxgi_mean_ns else 0.0
            )

            print("DXGI vsync (from SyncQPCTime / SyncRefreshCount):")
            print(f"  mean:    {fmt_ms(dxgi_mean_ns)}    (~{dxgi_fps:.3f} Hz)")
            print(f"  Qt - DXGI diff: {dxgi_diff_pct:+.3f}%")
            print()

    # ── Verdict ────────────────────────────────────────────────────────────
    print("=" * 60)
    print("VERDICT")
    print("=" * 60)

    qt_stddev_ms = qt_stddev_ns / 1e6
    outlier_pct = 100.0 * outliers / n if n else 0.0

    if not have_dxgi:
        print("DXGI stats unavailable (Qt internals didn't expose swap chain).")
        print("Decision based on Qt timing alone.")
        print()

    if outlier_pct > 5.0:
        print("FAIL: too many outlier intervals (>5% of frames missed/skipped).")
        print("      Likely cause: GPU driver, fullscreen exclusive vs windowed,")
        print("      or other process stealing GPU time. Investigate before Phase 1.")
    elif have_dxgi and abs(dxgi_diff_pct) > 0.1:
        print("WARN: Qt timing diverges from DXGI by more than 0.1%.")
        print("      Qt's render() interval is not a clean reflection of vsync.")
        print("      RECOMMENDATION: refactor FrameCanvas to own a D3D11 swap chain")
        print("      directly. Adds ~1 week to the display-resample plan.")
    elif qt_stddev_ms < 1.0:
        print("PASS: Qt timing is clean (stddev < 1ms), matches DXGI within tolerance.")
        print("      RECOMMENDATION: Proceed with Phase 1 of the display-resample plan.")
    elif qt_stddev_ms < 5.0:
        print("PASS (with caveat): Qt timing is workable but jittery (stddev 1-5ms).")
        print("      RECOMMENDATION: Proceed with Phase 1, but plan to add a")
        print("      rolling-mean filter in Phase 3 for stable interval estimates.")
    else:
        print("FAIL: Qt timing is too jittery (stddev > 5ms).")
        print("      Even with filtering, this won't give us frame-accurate pacing.")
        print("      Investigate: is timeBeginPeriod(1) active? Is another process")
        print("      hogging the CPU? Are we in fullscreen?")

    print()
    print("Pass this output back to Agent 3 for the next-step decision.")


if __name__ == "__main__":
    main(sys.argv)
