#!/usr/bin/env python3
"""Analyze a Velox movement run recorded with `velox.move.Record <file.csv>` (docs/movement-tooling.md).

CSV columns: time, st (script time, -1 outside a script), x, y, z, vx, vy, vz, speed2d, speed_ups, onground, grace,
latch, corrections, corr_last_cm, corr_max_cm.

Examples:
  Tools/analyze_run.py Saved/Runs/strafe.csv
  Tools/analyze_run.py Saved/Runs/strafe.csv --ground-speed 800 --min-peak-factor 1.4 --max-corrections 3 --max-correction-cm 30

Prints peak speed, speed at distance markers along an axis, correction statistics, and a PASS/FAIL line per threshold given.
Exit status 1 if any given threshold fails, 2 if the file cannot be parsed.
"""
import argparse
import csv
import sys


def load(path):
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        rows = []
        for r in reader:
            try:
                rows.append({k: float(v) for k, v in r.items()})
            except (TypeError, ValueError):
                continue
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv")
    ap.add_argument("--ground-speed", type=float, default=800.0, help="MaxGroundSpeed in cm/s (default 800)")
    ap.add_argument("--markers", type=float, default=512.0, help="marker spacing in cm along --axis (default 512)")
    ap.add_argument("--axis", choices=("x", "y"), default="x", help="axis the markers lie on (default x)")
    ap.add_argument("--all", action="store_true", help="include samples outside the script (st < 0)")
    ap.add_argument("--min-peak-factor", type=float, help="PASS if peak speed2d >= factor * ground speed")
    ap.add_argument("--min-peak", type=float, help="PASS if peak speed2d >= this many cm/s")
    ap.add_argument("--max-corrections", type=int, help="PASS if the number of corrections <= N")
    ap.add_argument("--max-correction-cm", type=float, help="PASS if the largest correction <= X cm")
    ap.add_argument("--mean-correction-cm", type=float, help="PASS if the mean correction <= Y cm")
    args = ap.parse_args()

    try:
        rows = load(args.csv)
    except OSError as e:
        print(f"cannot read {args.csv}: {e}", file=sys.stderr)
        return 2
    if not rows:
        print(f"{args.csv}: no data rows", file=sys.stderr)
        return 2
    scripted = [r for r in rows if r.get("st", -1) >= 0]
    if scripted and not args.all:
        rows = scripted

    t0 = rows[0]["time"]
    peak = max(rows, key=lambda r: r["speed2d"])
    print(f"file        {args.csv}")
    print(f"samples     {len(rows)} over {rows[-1]['time'] - t0:.2f} s" + (" (script samples only)" if scripted and not args.all else ""))
    print(f"start       x={rows[0]['x']:.0f} y={rows[0]['y']:.0f} z={rows[0]['z']:.0f}   end x={rows[-1]['x']:.0f} y={rows[-1]['y']:.0f} z={rows[-1]['z']:.0f}")
    print(f"peak speed  {peak['speed2d']:.1f} cm/s ({peak['speed_ups']:.0f} ups) at t={peak['time'] - t0:.2f} s, "
          f"{args.axis}={peak[args.axis]:.0f}   = {peak['speed2d'] / args.ground_speed:.2f} x ground speed {args.ground_speed:.0f}")

    print(f"speed at {args.axis} markers every {args.markers:.0f} cm (first crossing):")
    k = 1
    last_pos = rows[0][args.axis]
    for r in rows:
        pos = r[args.axis]
        while pos >= k * args.markers and last_pos < k * args.markers:
            print(f"  {args.axis}={k * args.markers:6.0f}  t={r['time'] - t0:5.2f} s  speed2d={r['speed2d']:7.1f} cm/s  ({r['speed2d'] / args.ground_speed:.2f}x)")
            k += 1
        last_pos = pos

    # Corrections: the counter is cumulative; count the increments inside the analysed window.
    c_first = rows[0]["corrections"]
    c_last = rows[-1]["corrections"]
    n_corr = int(c_last - c_first)
    sizes = []
    prev = c_first
    for r in rows:
        if r["corrections"] > prev:
            sizes.append(r["corr_last_cm"])
            prev = r["corrections"]
    cmax = max(sizes) if sizes else 0.0
    cmean = sum(sizes) / len(sizes) if sizes else 0.0
    print(f"corrections {n_corr}   max {cmax:.2f} cm   mean {cmean:.2f} cm" + ("" if sizes else "   (none, or recorded on an authority)"))
    in_air = sum(1 for r in rows if r["onground"] < 0.5)
    print(f"airborne    {100.0 * in_air / len(rows):.0f} % of samples; max knockback grace {max(r['grace'] for r in rows):.3f} s; latch set in {sum(1 for r in rows if r['latch'] > 0.5)} samples")

    failed = False

    def check(name, ok, detail):
        nonlocal failed
        print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
        failed |= not ok

    if args.min_peak_factor is not None:
        check("peak factor", peak["speed2d"] >= args.min_peak_factor * args.ground_speed,
              f"{peak['speed2d'] / args.ground_speed:.2f}x >= {args.min_peak_factor:.2f}x")
    if args.min_peak is not None:
        check("peak speed", peak["speed2d"] >= args.min_peak, f"{peak['speed2d']:.0f} >= {args.min_peak:.0f} cm/s")
    if args.max_corrections is not None:
        check("corrections", n_corr <= args.max_corrections, f"{n_corr} <= {args.max_corrections}")
    if args.max_correction_cm is not None:
        check("max correction", cmax <= args.max_correction_cm, f"{cmax:.2f} <= {args.max_correction_cm:.2f} cm")
    if args.mean_correction_cm is not None:
        check("mean correction", cmean <= args.mean_correction_cm, f"{cmean:.2f} <= {args.mean_correction_cm:.2f} cm")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
