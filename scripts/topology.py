"""Linux CPU-topology discovery and deterministic worker placement."""

from __future__ import annotations

import os
from pathlib import Path


def physical_cores_by_socket() -> dict[int, list[int]]:
    """Return one allowed logical CPU for each physical core, grouped by socket."""
    allowed = set(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else set()
    topology: dict[int, dict[int, int]] = {}
    for cpu_dir in sorted(Path("/sys/devices/system/cpu").glob("cpu[0-9]*")):
        cpu = int(cpu_dir.name[3:])
        if allowed and cpu not in allowed:
            continue
        topology_dir = cpu_dir / "topology"
        try:
            socket = int((topology_dir / "physical_package_id").read_text().strip())
            core = int((topology_dir / "core_id").read_text().strip())
        except (OSError, ValueError):
            continue
        topology.setdefault(socket, {}).setdefault(core, cpu)
    return {socket: sorted(cores.values()) for socket, cores in sorted(topology.items())}


def choose_cpus(placement: str, producers: int, consumers: int) -> list[int]:
    """Choose producer CPUs followed by consumer CPUs for the requested placement."""
    if placement == "none":
        return []

    sockets = physical_cores_by_socket()
    if not sockets:
        raise RuntimeError("Linux sysfs CPU topology is unavailable")

    if placement == "same-socket":
        for cpus in sockets.values():
            needed = producers + consumers
            if len(cpus) >= needed:
                return cpus[:needed]
        raise RuntimeError("no socket has enough physical cores for this configuration")

    if placement == "cross-socket":
        if len(sockets) < 2:
            raise RuntimeError("cross-socket placement requires at least two CPU sockets")
        socket_groups = list(sockets.values())
        if len(socket_groups[0]) < producers or len(socket_groups[1]) < consumers:
            raise RuntimeError("the first two sockets do not have enough physical cores")
        return socket_groups[0][:producers] + socket_groups[1][:consumers]

    raise ValueError(f"unknown placement: {placement}")
