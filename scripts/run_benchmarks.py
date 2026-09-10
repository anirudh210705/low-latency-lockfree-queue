#!/usr/bin/env python3
"""Run a reproducible queue throughput matrix and save raw CSV plus metadata."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import os
import platform
import subprocess
import sys
from pathlib import Path

from topology import choose_cpus, physical_cores_by_socket


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("results/raw"))
    parser.add_argument("--operations", type=int, default=1_000_000)
    parser.add_argument("--capacity", type=int, default=65_536)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--payloads", default="8,64,256")
    parser.add_argument("--thread-pairs", default="1,2,4,8,12")
    parser.add_argument(
        "--placement", choices=("none", "same-socket", "cross-socket"), default="none"
    )
    return parser.parse_args()


def invoke(binary: Path, queue: str, producers: int, consumers: int, payload: int,
           args: argparse.Namespace) -> dict[str, object]:
    cpus = choose_cpus(args.placement, producers, consumers)
    command = [
        str(binary), "--queue", queue,
        "--producers", str(producers), "--consumers", str(consumers),
        "--operations", str(args.operations), "--capacity", str(args.capacity),
        "--payload", str(payload),
    ]
    if cpus:
        command += ["--cpus", ",".join(map(str, cpus))]
    completed = subprocess.run(command, check=True, capture_output=True, text=True)
    result = json.loads(completed.stdout)
    result["placement"] = args.placement
    result["cpus"] = ",".join(map(str, cpus))
    return result


def machine_metadata() -> dict[str, object]:
    metadata: dict[str, object] = {
        "timestamp_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "platform": platform.platform(),
        "python": platform.python_version(),
        "logical_cpus": os.cpu_count(),
        "physical_cores_by_socket": physical_cores_by_socket(),
    }
    try:
        metadata["lscpu"] = subprocess.run(
            ["lscpu"], check=True, capture_output=True, text=True
        ).stdout
    except (FileNotFoundError, subprocess.CalledProcessError):
        metadata["lscpu"] = None
    return metadata


def main() -> int:
    args = parse_args()
    binary = args.binary.resolve()
    if not binary.is_file():
        raise FileNotFoundError(binary)
    if args.operations <= 0 or args.capacity <= 0 or args.repetitions <= 0 or args.warmups < 0:
        raise ValueError("numeric arguments must be positive (warmups may be zero)")

    payloads = [int(value) for value in args.payloads.split(",")]
    pairs = [int(value) for value in args.thread_pairs.split(",")]
    configurations = [("spsc", 1, 1)]
    configurations += [(queue, count, count) for count in pairs for queue in ("mutex", "mpmc")]

    args.output_dir.mkdir(parents=True, exist_ok=True)
    run_id = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    csv_path = args.output_dir / f"benchmark-{run_id}.csv"
    metadata_path = args.output_dir / f"benchmark-{run_id}.json"
    rows: list[dict[str, object]] = []

    for queue, producers, consumers in configurations:
        for payload in payloads:
            print(f"{queue} {producers}P/{consumers}C {payload}B", file=sys.stderr)
            for _ in range(args.warmups):
                invoke(binary, queue, producers, consumers, payload, args)
            for repetition in range(args.repetitions):
                row = invoke(binary, queue, producers, consumers, payload, args)
                row["repetition"] = repetition
                rows.append(row)

    if not rows:
        raise RuntimeError("benchmark matrix produced no rows")
    with csv_path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    metadata = machine_metadata()
    metadata["arguments"] = vars(args) | {"binary": str(binary), "output_dir": str(args.output_dir)}
    metadata_path.write_text(json.dumps(metadata, indent=2, default=str) + "\n")
    print(csv_path)
    print(metadata_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
