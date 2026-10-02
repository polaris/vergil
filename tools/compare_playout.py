#!/usr/bin/env python3
"""Compares the playout logs of receivers on one host (receiver --log).

Each row of a log says when a callback is heard, in local and master time,
and the rate loop's error. The frame heard has presentation time

    heard_master - error / sender_rate

and on a single host local time is master time, so each receiver's true
timing error, clock-sync error included, is

    heard_local - (heard_master - error / sender_rate)

The offset between two receivers is the difference of their true errors at
the same moment, bar their output latencies, which no log can see.

    compare_playout.py -s 44100 a.csv b.csv [--settle 10]
"""

import argparse
import sys

import numpy as np


def load(path):
    data = np.genfromtxt(path, delimiter=",", names=True)
    if data.size < 2:
        sys.exit(f"{path}: fewer than two rows")
    return data


def describe(label, values_us):
    return (f"{label:24s} mean {np.mean(values_us):+8.1f} us  sd {np.std(values_us):6.1f} us  "
            f"min {np.min(values_us):+8.1f}  max {np.max(values_us):+8.1f}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="+", help="playout logs, one per receiver")
    parser.add_argument("-s", "--sender-rate", type=float, required=True, help="the sender's sample rate, Hz")
    parser.add_argument("--settle", type=float, default=10.0,
                        help="seconds to skip after each receiver's first row (default 10)")
    parser.add_argument("--window", type=float, default=60.0,
                        help="seconds per row of the offset-over-time table (default 60)")
    args = parser.parse_args()

    logs = []
    for path in args.logs:
        d = load(path)
        t = d["heard_master_s"]
        keep = t >= t[0] + args.settle
        d = d[keep]
        if d.size < 2:
            sys.exit(f"{path}: nothing left after the settle time")
        loop = d["error_frames"] / args.sender_rate
        clock = d["heard_local_s"] - d["heard_master_s"]
        logs.append({"path": path, "t": d["heard_master_s"], "loop": loop, "clock": clock, "true": clock + loop})

    print("Per receiver (single host: true = clock-sync error + loop error)")
    for log in logs:
        print(f"  {log['path']}: {log['t'][-1] - log['t'][0]:.0f} s")
        print("    " + describe("loop error", log["loop"] * 1e6))
        print("    " + describe("clock-sync error", log["clock"] * 1e6))
        print("    " + describe("true error", log["true"] * 1e6))

    if len(logs) < 2:
        return

    a = logs[0]
    for b in logs[1:]:
        start = max(a["t"][0], b["t"][0])
        end = min(a["t"][-1], b["t"][-1])
        if end <= start:
            print(f"\n{a['path']} and {b['path']} do not overlap in time")
            continue
        at = (a["t"] >= start) & (a["t"] <= end)
        t = a["t"][at]
        # Positive: a plays later than b.
        offset = a["true"][at] - np.interp(t, b["t"], b["true"])

        print(f"\nOffset {a['path']} - {b['path']} over {end - start:.0f} s (positive: the first plays later)")
        print("  " + describe("offset", offset * 1e6))
        # A flat trend means both are locked to the same timeline.
        slope = np.polyfit(t - t[0], offset, 1)[0]
        print(f"  {'trend':24s} {slope * 1e6 * 60:+8.2f} us per minute")

        print(f"\n  {'from s':>7s} {'mean us':>9s} {'sd us':>7s} {'min us':>9s} {'max us':>9s}")
        for w0 in np.arange(0.0, end - start, args.window):
            w = (t - t[0] >= w0) & (t - t[0] < w0 + args.window)
            if np.count_nonzero(w) < 2:
                continue
            o = offset[w] * 1e6
            print(f"  {w0:7.0f} {np.mean(o):+9.1f} {np.std(o):7.1f} {np.min(o):+9.1f} {np.max(o):+9.1f}")


if __name__ == "__main__":
    main()
