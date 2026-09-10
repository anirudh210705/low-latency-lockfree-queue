#!/usr/bin/env python3
"""Plot median throughput with min/max error bars from a benchmark CSV."""

from __future__ import annotations

import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("results/plots/throughput.png"))
    parser.add_argument("--payload", type=int, default=64)
    args = parser.parse_args()

    grouped: dict[tuple[str, int], list[float]] = defaultdict(list)
    with args.csv.open(newline="") as source:
        for row in csv.DictReader(source):
            if int(row["payload_bytes"]) == args.payload:
                threads = int(row["producers"]) + int(row["consumers"])
                grouped[(row["queue"], threads)].append(float(row["throughput_mops"]))

    for queue in ("mutex", "spsc", "mpmc"):
        points = sorted((threads, values) for (name, threads), values in grouped.items()
                        if name == queue)
        if not points:
            continue
        x = [threads for threads, _ in points]
        medians = [statistics.median(values) for _, values in points]
        lower = [median - min(values) for median, (_, values) in zip(medians, points)]
        upper = [max(values) - median for median, (_, values) in zip(medians, points)]
        plt.errorbar(x, medians, yerr=[lower, upper], marker="o", capsize=3, label=queue)

    plt.xlabel("Worker threads (producers + consumers)")
    plt.ylabel("Throughput (million queue operations/second)")
    plt.title(f"Bounded queue throughput ({args.payload}-byte payload)")
    plt.grid(alpha=0.25)
    plt.legend()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    plt.tight_layout()
    plt.savefig(args.output, dpi=160)
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
